#!/usr/bin/env bash
# Builds heph-core: checks the laws, emits C, compiles the native binary.
#   core/build/heph-core.c   emitted C (what wheel builds compile, no bend needed)
#   src/heph/_bin/heph-core  dev binary
# Needs bend (pinned 2.0.32) and clang; CC overrides the compiler.
set -euo pipefail
core="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$core")"
mkdir -p "$core/build" "$root/src/heph/_bin"
cd "$core"
bend PROOF.bend
bend main.bend -o "$core/build/heph-core.c"
"${CC:-clang}" -O2 "$core/build/heph-core.c" -o "$root/src/heph/_bin/heph-core" -lm -lpthread
