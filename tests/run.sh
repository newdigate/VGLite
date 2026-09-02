#!/bin/sh
# Builds and runs VGLite's host test suite; exits non-zero on any failure.
# Copyright (c) 2026 Nicholas Newdigate
# SPDX-License-Identifier: MIT
#
# Covers port/vglite_guard.h's path validator, which is PURE by design so it
# needs no driver, no target and no GPU. That matters: the rt1176-evkb tree's
# QEMU gates all run the software engine, so none of them can see guard code.
set -eu
cd "$(dirname "$0")/.."
# mktemp, not a fixed /tmp name: two checkouts (or two concurrent runs) would
# otherwise race on one binary, and a fixed path writes outside the repo.
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT INT TERM HUP
cc -std=c99 -Wall -Wextra -Werror -o "$out/vglite_guard_test" tests/vglite_guard_test.c
"$out/vglite_guard_test"
