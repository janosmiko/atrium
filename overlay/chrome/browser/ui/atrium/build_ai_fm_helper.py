#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Compiles atrium_ai_foundation_models.swift with swiftc directly - a plain
action(), since gn itself crashes on a mac target declaring a .swift source.
"""

import argparse
import subprocess
import sys

_ARCH_BY_CPU = {
    'arm64': 'arm64',
    'x64': 'x86_64',
}


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('--source', required=True)
  parser.add_argument('--output', required=True)
  parser.add_argument('--target-cpu', required=True)
  parser.add_argument('--deployment-target', required=True)
  args = parser.parse_args()

  arch = _ARCH_BY_CPU.get(args.target_cpu)
  if not arch:
    print('build_ai_fm_helper.py: unsupported target_cpu %r' %
          args.target_cpu, file=sys.stderr)
    return 1

  target = '%s-apple-macos%s' % (arch, args.deployment_target)
  result = subprocess.run(
      ['xcrun', 'swiftc', '-O', '-target', target, args.source, '-o',
       args.output],
      capture_output=True, text=True, check=False)
  if result.returncode != 0:
    sys.stderr.write(result.stdout)
    sys.stderr.write(result.stderr)
  return result.returncode


if __name__ == '__main__':
  sys.exit(main())
