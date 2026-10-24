// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Decodes Mozilla's mozLz4 files: an 8-byte magic, the decoded size as a
//! little-endian u32, then one LZ4 block.
//!
//! The bytes come from another browser's profile, so every length and offset
//! is checked, and the output never grows past the declared size.

const MAGIC: &[u8; 8] = b"mozLz40\0";
const HEADER_BYTES: usize = 12;
/// A session file is a few megabytes at most. The cap stops a header that
/// asks for a huge buffer before anything is allocated.
pub const MAX_DECODED_BYTES: usize = 64 * 1024 * 1024;
const MIN_MATCH: usize = 4;

pub fn decode(input: &[u8]) -> Option<Vec<u8>> {
    if input.len() < HEADER_BYTES || !input.starts_with(MAGIC) {
        return None;
    }
    let size = u32::from_le_bytes([input[8], input[9], input[10], input[11]]) as usize;
    if size > MAX_DECODED_BYTES {
        return None;
    }
    decode_block(&input[HEADER_BYTES..], size)
}

fn decode_block(src: &[u8], size: usize) -> Option<Vec<u8>> {
    let mut out = Vec::with_capacity(size);
    let mut i = 0;
    while i < src.len() {
        let token = src[i];
        i += 1;
        let literals = length(usize::from(token >> 4), src, &mut i)?;
        let end = i.checked_add(literals).filter(|&end| end <= src.len())?;
        if literals > size - out.len() {
            return None;
        }
        out.extend_from_slice(&src[i..end]);
        i = end;
        // The last sequence holds literals only.
        if i == src.len() {
            break;
        }
        let offset = usize::from(u16::from_le_bytes([*src.get(i)?, *src.get(i + 1)?]));
        i += 2;
        if offset == 0 || offset > out.len() {
            return None;
        }
        let matched = length(usize::from(token & 0x0F), src, &mut i)?.checked_add(MIN_MATCH)?;
        if matched > size - out.len() {
            return None;
        }
        let start = out.len() - offset;
        // Byte by byte: a match can overlap the bytes it writes.
        for k in start..start + matched {
            let byte = out[k];
            out.push(byte);
        }
    }
    (out.len() == size).then_some(out)
}

/// A 4-bit length, plus the extension bytes that follow when it is 15.
fn length(mut n: usize, src: &[u8], i: &mut usize) -> Option<usize> {
    if n == 15 {
        loop {
            let byte = *src.get(*i)?;
            *i += 1;
            n = n.checked_add(usize::from(byte))?;
            if byte != 255 {
                break;
            }
        }
    }
    Some(n)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn moz(size: u32, block: &[u8]) -> Vec<u8> {
        [MAGIC.as_slice(), &size.to_le_bytes(), block].concat()
    }

    #[test]
    fn a_literal_only_block_decodes() {
        assert_eq!(decode(&moz(5, b"\x50hello")).unwrap(), b"hello");
        assert_eq!(decode(&moz(0, b"")).unwrap(), b"");
    }

    #[test]
    fn a_match_repeats_earlier_bytes() {
        // Three literals, then nine bytes copied from three back.
        assert_eq!(
            decode(&moz(12, b"\x35abc\x03\x00")).unwrap(),
            b"abcabcabcabc"
        );
    }

    #[test]
    fn an_overlapping_match_repeats_one_byte() {
        assert_eq!(decode(&moz(10, b"\x15a\x01\x00")).unwrap(), b"aaaaaaaaaa");
    }

    #[test]
    fn long_lengths_use_extension_bytes() {
        let text = vec![b'x'; 300];
        let block = [&[0xF0, 255, 30][..], &text].concat(); // 15 + 255 + 30 literals
        assert_eq!(decode(&moz(300, &block)).unwrap(), text);
        // One literal, then a match of 4 + 15 + 255 + 10 = 284 bytes at offset 1.
        assert_eq!(
            decode(&moz(285, &[0x1F, b'z', 1, 0, 255, 10])).unwrap(),
            vec![b'z'; 285]
        );
    }

    #[test]
    fn a_bad_header_is_refused() {
        assert!(decode(b"mozLz40").is_none(), "short header");
        assert!(
            decode(&[b"mozLz41\0".as_slice(), &5u32.to_le_bytes(), b"\x50hello"].concat())
                .is_none()
        );
        assert!(decode(&moz(u32::MAX, b"")).is_none());
        assert!(decode(&moz(MAX_DECODED_BYTES as u32 + 1, b"")).is_none());
    }

    #[test]
    fn a_block_that_disagrees_with_the_header_is_refused() {
        assert!(
            decode(&moz(6, b"\x50hello")).is_none(),
            "shorter than declared"
        );
        assert!(
            decode(&moz(4, b"\x50hello")).is_none(),
            "longer than declared"
        );
        assert!(decode(&moz(5, b"\x50hel")).is_none(), "truncated literals");
        assert!(decode(&moz(8, b"\x10a\x00\x00")).is_none(), "offset 0");
        assert!(
            decode(&moz(8, b"\x10a\x02\x00")).is_none(),
            "offset before the start"
        );
        assert!(decode(&moz(8, b"\x10a\x01")).is_none(), "truncated offset");
        assert!(
            decode(&moz(300, &[0xF0, 255])).is_none(),
            "truncated length"
        );
    }

    #[test]
    fn no_input_makes_the_decoder_panic() {
        let valid = moz(12, b"\x35abc\x03\x00");
        for end in 0..valid.len() {
            let _ = decode(&valid[..end]);
        }
        let mut seed: u64 = 0x9E37_79B9_7F4A_7C15;
        for _ in 0..2000 {
            let mut block = Vec::new();
            for _ in 0..(seed % 64) {
                seed ^= seed << 13;
                seed ^= seed >> 7;
                seed ^= seed << 17;
                block.push(seed as u8);
            }
            let _ = decode(&moz((seed % 4096) as u32, &block));
        }
    }
}
