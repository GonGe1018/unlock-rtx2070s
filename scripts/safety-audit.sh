#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
source_file=src/preflight.c

# These interfaces are outside the read-only preflight boundary. Keep the
# expressions split so this audit script does not trigger its own search.
forbidden=(
    'Pci[.]Wri''te'
    'Mem[.](Read|Wri''te)'
    'Io[.](Read|Wri''te)'
    '[-][>]Map[(]'
    'AllocateBuffer'
    'SetBarAttributes'
    'SetVariable'
    'ResetSystem'
    'LoadImage'
    'StartImage'
)

for expression in "${forbidden[@]}"; do
    if grep -En -- "$expression" "$source_file"; then
        echo "safety audit: forbidden interface matched: $expression" >&2
        exit 1
    fi
done

grep -q -- 'Pci[.]Read' "$source_file"
grep -q -- 'StrCmp(info->VolumeLabel, L"RTX2070S")' "$source_file"
grep -q -- 'tu104_identity_matches' "$source_file"

echo 'safety audit: read-only boundary passed'
