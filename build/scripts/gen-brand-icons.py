#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Generates the 22 AppIconVariant SVG sources from Brand Book v3's murmuration
mark (docs/brand/brand-book.html, `flow()`/`renderVals()`). Ports that logic
exactly so the app icons and the brand book stay in lockstep - regenerate
here rather than hand-editing build/icons-src/*.svg. The two must emit the
same bytes for a given ground and tone set.

Frame: each SVG keeps the shared 140x140 viewBox with the rounded-rect
background clipped at x=4 y=4 w=132 h=132 rx=30 (see build/scripts/mask-icon-alpha.py,
which assumes exactly that frame when punching alpha back into the
qlmanage-rendered PNGs).

Two outputs per variant, from the same geometry:

  build/icons-src/<slug>.svg        one flat tile, rasterised by build-icons.sh
                                    into the settings pane's picker PNGs.
  build/icons-src/<Name>.icon/      an Icon Composer package that
                                    build-assets-car.sh hands to actool, so the
                                    Dock gets a real Liquid Glass icon instead
                                    of a flat picture of one.

The .icon splits the flat tile across the system's compositor: its base and
vertical falloff become the `fill` gradient, the corner wash becomes a plain
layer, and the flock stays a flat layer on top of the glass tile.

The 22nd variant, `auto`, is two of those stacks in one package: the
crimson-paper wash and mark for the light appearance, the crimson-ink ones for
the dark, each pair switched by `hidden-specializations`. The top-level `fill`
paints only one appearance, so the ink base rides a full-bleed layer instead.
"""
import decimal
import json
import math
import os
import shutil
import unicodedata

# Locked mark geometry from the brand book (docs/brand/brand-book.html), v3.
ARMS = 4
HEAD = 48
TAIL = 24
WEIGHT = 1.25
ARM_SEED = 3
TAIL_SEED = 980
# Mark width over tile width lands near Chrome's 0.86 in the flat tile and
# 0.78 in the catalog, which insets the same viewBox further.
MARK_SCALE = 2.4
SVG_OPEN = '<svg width="1024" height="1024" viewBox="0 0 140 140" xmlns="http://www.w3.org/2000/svg">\n'

# actool 26.6 accepts only "display-p3" for this key. "srgb", "extended-srgb"
# and "extended-gray" each kill ibtoold with a nil-object exception rather than
# a validation error, so a typo here looks like a compiler crash.
SVG_COLOR_SPACE = "display-p3"

# The paper tile is the only see-through one, Arc-style. Icon Composer's
# slider: 1.0 is the most see-through. Only the Dock applies it, an offline
# imageNamed: render ignores it.
PAPER_TRANSLUCENCY = 1.0

# --app-icon and Info.plist's CFBundleIconName both name this stack, so the
# default variant is emitted a second time under it - a bundle that sets no
# icon of its own then shows the light tile in either system appearance.
BUNDLE_ICON_NAME = "AppIcon"
DEFAULT_VARIANT = "crimson-paper"
AUTO_VARIANT = "auto"
AUTO_LIGHT_VARIANT = "crimson-paper"
AUTO_DARK_VARIANT = "crimson-ink"

THEMES = {
    "Crimson": {"ink": "#221016", "paper": "#F9F0F1", "accent": "#B03A48", "soft": "#E06A76"},
    "Vermilion": {"ink": "#1D0D06", "paper": "#FFF2EA", "accent": "#E8481C", "soft": "#FF8A5C"},
    "Magenta": {"ink": "#23081E", "paper": "#FBEFF7", "accent": "#D62D8A", "soft": "#F27AB8"},
    "Ultraviolet": {"ink": "#120E2E", "paper": "#F0EEFB", "accent": "#6C4CE8", "soft": "#9D8AF2"},
    "Cyan": {"ink": "#04212B", "paper": "#EAF7F9", "accent": "#00A6C0", "soft": "#56D0E2"},
    "Lime": {"ink": "#131A06", "paper": "#F5F8EA", "accent": "#7CB518", "soft": "#A9D94E"},
    "Amber": {"ink": "#1D1406", "paper": "#FBF4E6", "accent": "#E8960C", "soft": "#F5B84D"},
    "Tokyo Night": {"ink": "#1A1B26", "paper": "#E6E9F5", "accent": "#7AA2F7", "soft": "#BB9AF7"},
    "Catppuccin": {"ink": "#1E1E2E", "paper": "#EFF1F5", "accent": "#CBA6F7", "soft": "#F5C2E7"},
    "Gruvbox": {"ink": "#282828", "paper": "#FBF1C7", "accent": "#D65D0E", "soft": "#FABD2F"},
    "Kanagawa": {"ink": "#1F1F28", "paper": "#DCD7BA", "accent": "#7E9CD8", "soft": "#957FB8"},
    "Synthwave Everything": {"ink": "#241B2F", "paper": "#F6EBFF", "accent": "#FF7EDB", "soft": "#72F1B8"},
    "Bluloco": {"ink": "#282C34", "paper": "#F9F9F9", "accent": "#3691FF", "soft": "#9F7EFE"},
    "Monokai Pro": {"ink": "#2D2A2E", "paper": "#FCFCFA", "accent": "#FF6188", "soft": "#FFD866"},
    "Dracula": {"ink": "#282A36", "paper": "#F8F8F2", "accent": "#BD93F9", "soft": "#FF79C6"},
    "Nord": {"ink": "#2E3440", "paper": "#ECEFF4", "accent": "#88C0D0", "soft": "#81A1C1"},
    "Rosé Pine": {"ink": "#191724", "paper": "#FAF4ED", "accent": "#EB6F92", "soft": "#C4A7E7"},
    "Everforest": {"ink": "#2D353B", "paper": "#FDF6E3", "accent": "#A7C080", "soft": "#E69875"},
    "Solarized": {"ink": "#002B36", "paper": "#FDF6E3", "accent": "#268BD2", "soft": "#CB4B16"},
}

DARK_BASE = ["#FF5A6B", "#FFB52E", "#1FD1F0", "#9BF04A"]
LIGHT_BASE = ["#FF3B4E", "#FF9500", "#00B5E0", "#7ED321"]
# How far a flow tone bends toward the theme. The brand book pulled this back
# from 0.15 so the four arms stay distinct instead of collapsing toward one hue.
TONE_LERP = 0.05
MONO_STOPS = (0.16, 0.3, 0.44, 0.58)
# Brand Book section 07 (`iconTile`/`glowLayers` in docs/brand/brand-book.html):
# the Dark and Light glow sliders' defaults, and the fixed tile bases. Keep
# both files in step.
WASH_DARK = 0.2
WASH_LIGHT = 0.2
INK_BASE = "#2C2C2C"
PAPER_BASE = "#FFFFFF"


def slugify(name):
    """ASCII kebab-case: 'Rosé Pine' -> 'rose-pine', 'Tokyo Night' -> 'tokyo-night'."""
    ascii_name = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode("ascii")
    return ascii_name.lower().replace(" ", "-")


def js_round(x):
    """Math.round: ties go up, not to even, which is what Python's round() does."""
    return math.floor(x + 0.5)


def to_fixed(x, digits=2):
    """Number.prototype.toFixed on the exact double, ties up. Every value the
    mark formats is positive, so away-from-zero and toward-positive agree."""
    quantum = decimal.Decimal(1).scaleb(-digits)
    return str(decimal.Decimal(x).quantize(quantum, rounding=decimal.ROUND_HALF_UP))


def lerp_hex(a, b, t):
    out = "#"
    for i in (1, 3, 5):
        av = int(a[i:i + 2], 16)
        bv = int(b[i:i + 2], 16)
        out += format(js_round(av * (1 - t) + bv * t), "02x")
    return out


M32 = 0xFFFFFFFF


def rand(seed):
    """mulberry32, in unsigned 32-bit arithmetic so it matches the brand book's
    Math.imul version bit for bit."""
    state = seed & M32

    def nxt():
        nonlocal state
        state = (state + 0x6D2B79F5) & M32
        t = ((state ^ (state >> 15)) * (1 | state)) & M32
        t = ((t + (((t ^ (t >> 7)) * (61 | t)) & M32)) & M32) ^ t
        return (t ^ (t >> 14)) / 4294967296

    return nxt


def gauss(r):
    return r() + r() + r() - 1.5


# One comet. `d` is the chance of keeping a draw at position s along the sweep,
# `w` the cross-sweep spread, `sz` the bird size.
COMET = {
    "th0": 0, "th1": 128, "r0": 7.5, "r1": 22.5, "s0": 0,
    "d": lambda s: 1 if s < 0.18 else 0.42 * (1 - s) ** 1.9,
    "w": lambda s: 1.7 if s < 0.18 else 0.55 + 1.3 * (1 - s),
    "sz": lambda s: 1.25 if s < 0.18 else 0.92,
}
# The comet keeps nearly every draw at the head, so birds added to it pile onto
# the beginning. The tail pass starts past the head instead.
TAIL_CFG = {
    **COMET, "s0": 0.36,
    "d": lambda s: (1 - s) ** 0.7,
    "sz": lambda s: 0.86 - 0.3 * s,
}


def birds(cfg, n, rot, seed):
    r = rand(seed)
    out = []
    tries = 0
    while len(out) < n and tries < n * 45:
        tries += 1
        s = cfg["s0"] + (1 - cfg["s0"]) * r() if cfg["s0"] else r()
        if r() > cfg["d"](s):
            continue
        th = math.radians(cfg["th0"] + (cfg["th1"] - cfg["th0"]) * s) + math.radians(rot)
        rr = cfg["r0"] + (cfg["r1"] - cfg["r0"]) * s + gauss(r) * cfg["w"](s)
        out.append({
            "x": 24 + rr * math.cos(th), "y": 24 + rr * math.sin(th),
            "ang": th + math.pi / 2, "s": s,
            "size": (0.44 + r() * 0.82) * cfg["sz"](s),
            "op": 0.45 + r() * 0.55, "j": r(),
        })
    return out


def flock_mark(ground, tones, ink):
    """Returns the mark's inner SVG markup in the 48x48 space (Brand Book's `flow()`).

    On ink and accent a bird lifts all the way toward white. On paper it sinks
    partway toward the theme ink instead, because white would erase it.
    """
    hi = ink if ground == "paper" else "#FFFFFF"
    toward = 0.55 if ground == "paper" else 1
    els = []
    for k in range(ARMS):
        rot = k * (360 / ARMS)
        flock = (birds(COMET, HEAD, rot, ARM_SEED + k * 13)
                 + birds(TAIL_CFG, TAIL, rot, TAIL_SEED + k * 13))
        for b in flock:
            col = lerp_hex(tones[k % len(tones)], hi, toward * (0.1 + (1 - b["s"]) * 0.6 * b["j"]))
            els.append(
                f'<ellipse cx="{to_fixed(b["x"])}" cy="{to_fixed(b["y"])}" '
                f'rx="{to_fixed(b["size"] * WEIGHT * 1.85)}" ry="{to_fixed(b["size"] * WEIGHT)}" '
                f'transform="rotate({to_fixed(b["ang"] * 180 / math.pi, 1)} '
                f'{to_fixed(b["x"])} {to_fixed(b["y"])})" fill="{col}" '
                f'fill-opacity="{to_fixed(b["op"] * (0.5 + (1 - b["s"]) * 0.5))}"/>'
            )
    return "".join(els)


def theme_tones(theme):
    """Grayed ink/paper + the two tone families, exactly as renderVals() computes them."""
    ink = lerp_hex(theme["ink"], "#84878C", 0.14)
    paper = lerp_hex(theme["paper"], "#A6ABB3", 0.12)
    dark_t = [lerp_hex(c, theme["soft"], TONE_LERP) for c in DARK_BASE]
    mono_t = [lerp_hex("#FFFFFF", theme["accent"], t) for t in MONO_STOPS]
    return ink, paper, dark_t, mono_t


def variant_parts(theme_name, background):
    """The tile base, wash tones, wash amount and mark markup for one variant."""
    theme = THEMES[theme_name]
    accent = theme["accent"]
    ink, paper, dark_t, mono_t = theme_tones(theme)

    if background == "ink":
        tones, bg_rect, amount = dark_t, INK_BASE, WASH_DARK
    elif background == "paper":
        tones = [lerp_hex(c, theme["accent"], TONE_LERP) for c in LIGHT_BASE]
        bg_rect, amount = PAPER_BASE, WASH_LIGHT
    else:
        tones, bg_rect, amount = mono_t, accent, WASH_LIGHT

    return bg_rect, tones, amount, flock_mark(background, tones, ink)


def mark_group(mark):
    """Places the mark identically in the flat tile and in the .icon's mark
    layer, so the two renderings stay registered."""
    return (
        f'  <g transform="translate(70 70) scale({MARK_SCALE}) translate(-24 -24)">\n'
        f'    {mark}\n'
        '  </g>\n'
    )


def render_svg(parts):
    base, tones, amount, mark = parts
    return (
        f'{SVG_OPEN}'
        f'{brand_tile(base, tones, amount)}'
        f'{mark_group(mark)}'
        '</svg>\n'
    )


def wash_svg(parts):
    """The .icon's plain background layer: the corner wash with nothing under
    it, because the base and its falloff are the icon.json `fill` instead."""
    base, tones, amount, _ = parts
    return (
        f'{SVG_OPEN}'
        '  <defs>\n'
        f'{wash_gradients(base, tones, wash_opacity(base, amount))}'
        '  </defs>\n'
        # Full bleed rather than the flat tile's inset frame. The system clips
        # the layer to its own squircle, and an inset would strand an unwashed
        # rim along the edge the wash is brightest at.
        '  <rect width="140" height="140" fill="url(#washA)"/>\n'
        '  <rect width="140" height="140" fill="url(#washB)"/>\n'
        '</svg>\n'
    )


def mark_svg(parts):
    """The .icon's mark layer: the flock, painted flat over the glass tile."""
    return f'{SVG_OPEN}{mark_group(parts[3])}</svg>\n'


def base_svg(parts):
    """The .icon's opaque tile for the appearance the top-level `fill` does not
    paint. One `fill` gradient serves the whole stack, so the second appearance
    needs its base and falloff back as a full-bleed layer under the wash."""
    top, bottom = tile_fill_colors(parts[0])
    return (
        f'{SVG_OPEN}'
        '  <defs>\n'
        '    <linearGradient id="base" x1="0" y1="0" x2="0" y2="1">\n'
        f'      <stop offset="0" stop-color="{top}"/>\n'
        f'      <stop offset="1" stop-color="{bottom}"/>\n'
        '    </linearGradient>\n'
        '  </defs>\n'
        '  <rect width="140" height="140" fill="url(#base)"/>\n'
        '</svg>\n'
    )


def auto_svg(light, dark):
    """The picker's flat tile for `auto`: the light tile with the dark one laid
    over its bottom-right half, split on the diagonal that runs from the
    top-right corner to the bottom-left one. The mark is cut by the same
    diagonal, so both halves of the flock show at once."""
    return (
        f'{SVG_OPEN}'
        '  <defs>\n'
        '    <clipPath id="half">\n'
        '      <path d="M 140 0 L 140 140 L 0 140 Z"/>\n'
        '    </clipPath>\n'
        '  </defs>\n'
        f'{brand_tile(light[0], light[1], light[2], "L")}'
        f'{mark_group(light[3])}'
        '  <g clip-path="url(#half)">\n'
        f'{brand_tile(dark[0], dark[1], dark[2], "D")}'
        f'{mark_group(dark[3])}'
        '  </g>\n'
        '</svg>\n'
    )


def tile_fill_colors(base):
    """The flat tile's top and bottom edge colors, resolved to opaque hex. The
    .icon carries no base rect, so the falloff that would have been painted
    over one becomes the two stops of the icon.json `fill` gradient."""
    _, _, top, bottom = tile_falloff(base)
    return lerp_hex(base, "#FFFFFF", top), lerp_hex(base, "#000000", bottom)


def color_spec(hex_color):
    """Icon Composer's opaque color literal, e.g. '#FFFFFF' -> the 1,1,1,1 form."""
    parts = ",".join(f"{int(hex_color[i:i + 2], 16) / 255:.5f}" for i in (1, 3, 5))
    return f"extended-srgb:{parts},1.00000"


def icon_json(base, translucent):
    top, bottom = tile_fill_colors(base)
    return {
        "color-space-for-untagged-svg-colors": SVG_COLOR_SPACE,
        "fill": {"linear-gradient": [color_spec(top), color_spec(bottom)]},
        "groups": [
            {
                "hidden": False,
                # actool paints the first entry on top, so the mark leads. The mark
                # is not glass: the material's top-down light washed the lower
                # birds out over the white fill.
                "layers": [
                    {"glass": False, "hidden": False, "image-name": "mark.svg", "name": "mark"},
                    {"glass": False, "hidden": False, "image-name": "wash.svg", "name": "wash"},
                ],
                "shadow": {"kind": "neutral", "opacity": 0.5},
                "translucency": {
                    "enabled": translucent,
                    "value": PAPER_TRANSLUCENCY if translucent else 0.5,
                },
            }
        ],
        "supported-platforms": {"squares": ["macOS"]},
    }


def appearance_layer(name, image, dark):
    """One half of the auto stack: a layer that paints in exactly one system
    appearance. `hidden` is the light value and the specialization is the dark
    one, so the tinted appearance falls in with the light set."""
    return {
        "glass": False,
        "hidden": dark,
        "hidden-specializations": [{"appearance": "dark", "value": not dark}],
        "image-name": image,
        "name": name,
    }


def auto_icon_json(light_base):
    """The auto stack. The top-level `fill` takes no specializations, so it
    paints the light base and the dark one arrives as `tile-dark`, the bottom
    layer, which is opaque and covers the light fill wherever it shows."""
    top, bottom = tile_fill_colors(light_base)
    return {
        "color-space-for-untagged-svg-colors": SVG_COLOR_SPACE,
        "fill": {"linear-gradient": [color_spec(top), color_spec(bottom)]},
        "groups": [
            {
                "hidden": False,
                "layers": [
                    appearance_layer("mark", "mark.svg", dark=False),
                    appearance_layer("mark-dark", "mark-dark.svg", dark=True),
                    appearance_layer("wash", "wash.svg", dark=False),
                    appearance_layer("wash-dark", "wash-dark.svg", dark=True),
                    appearance_layer("tile-dark", "tile-dark.svg", dark=True),
                ],
                "shadow": {"kind": "neutral", "opacity": 0.5},
                "translucency": {"enabled": True, "value": PAPER_TRANSLUCENCY},
            }
        ],
        "supported-platforms": {"squares": ["macOS"]},
    }


def write_auto_icon_bundle(out_dir, name, light, dark):
    """Writes the two-appearance package under `name`."""
    bundle = os.path.join(out_dir, f"{name}.icon")
    assets = os.path.join(bundle, "Assets")
    os.makedirs(assets, exist_ok=True)
    for filename, body in (
        ("wash.svg", wash_svg(light)),
        ("mark.svg", mark_svg(light)),
        ("wash-dark.svg", wash_svg(dark)),
        ("mark-dark.svg", mark_svg(dark)),
        ("tile-dark.svg", base_svg(dark)),
    ):
        with open(os.path.join(assets, filename), "w") as f:
            f.write(body)
    with open(os.path.join(bundle, "icon.json"), "w") as f:
        json.dump(auto_icon_json(light[0]), f, indent=2, sort_keys=True)
        f.write("\n")
    return f"{name}.icon"


def write_icon_bundle(out_dir, name, parts, translucent):
    """Writes one Icon Composer package. The directory name becomes the stack's
    name in Assets.car, which is how the runtime reaches a variant by slug."""
    bundle = os.path.join(out_dir, f"{name}.icon")
    assets = os.path.join(bundle, "Assets")
    os.makedirs(assets, exist_ok=True)
    with open(os.path.join(assets, "wash.svg"), "w") as f:
        f.write(wash_svg(parts))
    with open(os.path.join(assets, "mark.svg"), "w") as f:
        f.write(mark_svg(parts))
    with open(os.path.join(bundle, "icon.json"), "w") as f:
        json.dump(icon_json(parts[0], translucent), f, indent=2, sort_keys=True)
        f.write("\n")
    return f"{name}.icon"


def tile_falloff(base):
    """The base's classification and its vertical falloff endpoints, as white
    and black opacities. The .icon has no base rect to paint these over, so it
    reads the same numbers back out as two solid gradient colors."""
    white = base == PAPER_BASE
    dark = not white and int(base[1:3], 16) < 128
    top, bottom = (0, 0.063) if white else (0.10, 0.35) if dark else (0.16, 0.10)
    return white, dark, top, bottom


def wash_opacity(base, amount):
    _, dark, _, _ = tile_falloff(base)
    return (0.9 if dark else 0.55) * amount


def wash_gradients(base, tones, op, suffix=""):
    """The ambilight corner wash, as `<defs>` entries at 4-space indent."""
    return (
        f'    <linearGradient id="washA{suffix}" x1="0" y1="0" x2="1" y2="1">\n'
        f'      <stop offset="0" stop-color="{tones[0]}" stop-opacity="{op:.3f}"/>\n'
        f'      <stop offset="0.5" stop-color="{base}" stop-opacity="0"/>\n'
        f'      <stop offset="1" stop-color="{tones[2]}" stop-opacity="{op:.3f}"/>\n'
        '    </linearGradient>\n'
        f'    <linearGradient id="washB{suffix}" x1="1" y1="0" x2="0" y2="1">\n'
        f'      <stop offset="0" stop-color="{tones[1]}" stop-opacity="{op * 0.8:.3f}"/>\n'
        f'      <stop offset="0.5" stop-color="{base}" stop-opacity="0"/>\n'
        f'      <stop offset="1" stop-color="{tones[3]}" stop-opacity="{op * 0.8:.3f}"/>\n'
        '    </linearGradient>\n'
    )


def brand_tile(base, tones, amount, suffix=""):
    """The brand book's `iconTile`: a flat base, the vertical falloff measured
    from Apple's Dock tiles (Safari #FFFFFF to #EFEFEF, Terminal #404040 to
    #191919, Mail #A1E3FF to #1B86FB), the ambilight corner wash at `amount`,
    and one rim. The frame stays x=4 y=4 w=132 h=132 rx=30 because
    build/scripts/mask-icon-alpha.py punches alpha back on that geometry.

    `suffix` disambiguates the gradient ids so the auto tile can carry two of
    these in one document without the second silently reusing the first's.
    """
    white, dark, top, bottom = tile_falloff(base)
    op = wash_opacity(base, amount)
    rim = "" if white else (
        '  <rect x="5" y="5" width="130" height="130" rx="29" fill="none"\n'
        '        stroke="#FFFFFF" stroke-opacity="0.22" stroke-width="2"/>\n'
    )
    return (
        '  <defs>\n'
        f'    <clipPath id="tile{suffix}">\n'
        '      <rect x="4" y="4" width="132" height="132" rx="30"/>\n'
        '    </clipPath>\n'
        f'    <linearGradient id="shade{suffix}" x1="0" y1="0" x2="0" y2="1">\n'
        f'      <stop offset="0" stop-color="#FFFFFF" stop-opacity="{top}"/>\n'
        '      <stop offset="0.5" stop-color="#FFFFFF" stop-opacity="0"/>\n'
        '      <stop offset="0.5" stop-color="#000000" stop-opacity="0"/>\n'
        f'      <stop offset="1" stop-color="#000000" stop-opacity="{bottom}"/>\n'
        '    </linearGradient>\n'
        f'{wash_gradients(base, tones, op, suffix)}'
        '  </defs>\n'
        f'  <g clip-path="url(#tile{suffix})">\n'
        f'    <rect x="4" y="4" width="132" height="132" fill="{base}"/>\n'
        f'    <rect x="4" y="4" width="132" height="132" fill="url(#shade{suffix})"/>\n'
        f'    <rect x="4" y="4" width="132" height="132" fill="url(#washA{suffix})"/>\n'
        f'    <rect x="4" y="4" width="132" height="132" fill="url(#washB{suffix})"/>\n'
        '  </g>\n'
        f'{rim}'
    )


# The AI panel's thinking loader. It draws the same mark at a bird count that
# still resolves beside a 12px label, and fattens each bird, because the shipped
# sizes fall under a pixel at 18px and the flock turns to haze.
LOADER_HEAD = 24
LOADER_TAIL = 12
LOADER_FATTEN = 1.25
LOADER_BANDS = 5
# How far the front lags from one arm to the next, as a share of its own period.
LOADER_SPREAD = 0.05
LOADER_JS = os.path.join(
    os.path.dirname(__file__), "..", "..",
    "overlay", "chrome", "browser", "resources", "atrium", "mark-flock.js")


def loader_rows():
    """One row per bird: arm, band, cx, cy, rx, ry, degrees, opacity. Band 0 is
    the one bird per arm nearest the centre, which never dims. Bands 1 to
    LOADER_BANDS hold the rest by distance, which is what staggers the loader's
    front through the depth of an arm instead of dimming the arm at once."""
    rows = []
    for k in range(ARMS):
        rot = k * (360 / ARMS)
        flock = (birds(COMET, LOADER_HEAD, rot, ARM_SEED + k * 13)
                 + birds(TAIL_CFG, LOADER_TAIL, rot, TAIL_SEED + k * 13))
        flock.sort(key=lambda b: math.hypot(b["x"] - 24, b["y"] - 24))
        outer = len(flock) - 1
        for i, b in enumerate(flock):
            band = 0 if i == 0 else 1 + (i - 1) * LOADER_BANDS // outer
            size = b["size"] * LOADER_FATTEN
            rows.append([
                k, band,
                float(to_fixed(b["x"])), float(to_fixed(b["y"])),
                float(to_fixed(size * 1.85)), float(to_fixed(size)),
                float(to_fixed(b["ang"] * 180 / math.pi, 1)),
                float(to_fixed(b["op"] * (0.5 + (1 - b["s"]) * 0.5))),
            ])
    return rows


def write_loader_js():
    """The loader takes the flow tones flat, with none of the lift toward white
    the icons carry. That lift reads as depth at 1024px and as washed-out at 18,
    and dropping it is what lets one tone set serve a light or a dark panel."""
    tones = [lerp_hex(c, THEMES["Crimson"]["soft"], TONE_LERP) for c in DARK_BASE]
    lines = [
        "// Copyright 2026 The Atrium Authors",
        "// Use of this source code is governed by the Apache License, Version 2.0",
        "// found in the LICENSE file.",
        "// GENERATED by build/scripts/gen-brand-icons.py - do not edit.",
        "",
        f"export const FLOCK_ARMS = {ARMS};",
        f"export const FLOCK_BANDS = {LOADER_BANDS};",
        f"export const FLOCK_SPREAD = {LOADER_SPREAD};",
        "",
        "export const FLOCK_TONES = [",
    ]
    lines += [f"  '{t}'," for t in tones]
    lines += [
        "];",
        "",
        "// arm, band (0 is the core bird, 1 the innermost ring), cx, cy, rx, ry, degrees, opacity",
        "export const FLOCK_BIRDS = [",
    ]
    for r in loader_rows():
        lines.append("  [" + ", ".join(
            str(int(v)) if i < 2 else str(v) for i, v in enumerate(r)) + "],")
    lines += ["];", ""]
    path = os.path.normpath(LOADER_JS)
    with open(path, "w") as f:
        f.write("\n".join(lines))
    return path


def variant_ids():
    """21 variants: the crimson-ink tile, the default crimson-paper light variant,
    plus every theme's -accent composition. User feedback on the original 38-icon
    set (all 19 themes x ink/accent) was that the 18 non-default -ink variants
    all read as near-identical dark tiles, so those are dropped - only the default
    ink composition (crimson-ink), the light paper composition (crimson-paper),
    and the higher-contrast accent compositions ship as choosable app icons.
    """
    yield "crimson-ink", "Crimson", "ink"
    yield "crimson-paper", "Crimson", "paper"
    for theme_name in THEMES:
        slug = slugify(theme_name)
        yield f"{slug}-accent", theme_name, "accent"


def main():
    out_dir = os.path.join(os.path.dirname(__file__), "..", "icons-src")
    out_dir = os.path.normpath(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    existing = {f for f in os.listdir(out_dir) if f.endswith((".svg", ".icon"))}
    written = set()
    by_id = {}
    for variant_id, theme_name, background in variant_ids():
        parts = variant_parts(theme_name, background)
        by_id[variant_id] = parts
        filename = f"{variant_id}.svg"
        path = os.path.join(out_dir, filename)
        with open(path, "w") as f:
            f.write(render_svg(parts))
        written.add(filename)
        print(f"wrote: {path}")

        written.add(write_icon_bundle(out_dir, variant_id, parts, background == "paper"))
        print(f"wrote: {os.path.join(out_dir, variant_id)}.icon")

    light, dark = by_id[AUTO_LIGHT_VARIANT], by_id[AUTO_DARK_VARIANT]
    auto_path = os.path.join(out_dir, f"{AUTO_VARIANT}.svg")
    with open(auto_path, "w") as f:
        f.write(auto_svg(light, dark))
    written.add(f"{AUTO_VARIANT}.svg")
    print(f"wrote: {auto_path}")
    written.add(write_auto_icon_bundle(out_dir, AUTO_VARIANT, light, dark))
    print(f"wrote: {os.path.join(out_dir, AUTO_VARIANT)}.icon")
    written.add(write_icon_bundle(out_dir, BUNDLE_ICON_NAME, by_id[DEFAULT_VARIANT], True))
    print(f"wrote: {os.path.join(out_dir, BUNDLE_ICON_NAME)}.icon")

    print(f"wrote: {write_loader_js()}")

    # Remove any stale v1-only SVGs (e.g. the old crimson-paper variant, which
    # v2 no longer generates).
    for stale in sorted(existing - written):
        path = os.path.join(out_dir, stale)
        shutil.rmtree(path) if stale.endswith(".icon") else os.remove(path)
        print(f"removed stale: {path}")


if __name__ == "__main__":
    main()
