#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

for required in README.md README.ko.md LICENSE NOTICE.md \
    docs/SAFETY.md docs/BUILD.md docs/VALIDATION.md docs/PORTING.md \
    docs/REPRODUCING.md docs/EXPERIMENT-HISTORY.md \
    docs/EXPERIMENT-HISTORY.ko.md \
    results/SHA256SUMS results/input-manifest.json \
    results/publication-checks.json results/rtx2070s-field-validation.json \
    results/experiment-history.json tools/verify_public_results.py; do
    test -s "$required"
done

(cd results && sha256sum -c SHA256SUMS)

python3 - <<'PY'
import json
import re
from pathlib import Path

root = Path(".")
for path in sorted((root / "results").glob("*.json")):
    with path.open("r", encoding="utf-8") as stream:
        json.load(stream)

missing = []
ignored_roots = {"artifacts", "build", "references", "third_party"}
for path in sorted(root.rglob("*.md")):
    if any(part in ignored_roots for part in path.parts):
        continue
    text = path.read_text(encoding="utf-8")
    for match in re.finditer(r"\[[^]]*\]\(([^)]+)\)", text):
        target = match.group(1).strip().strip("<>")
        if target.startswith(("http://", "https://", "mailto:", "#")):
            continue
        target = target.split("#", 1)[0]
        if target and not (path.parent / target).exists():
            missing.append(f"{path}: {target}")
if missing:
    raise SystemExit("missing local Markdown links:\n" + "\n".join(missing))
PY

for script in scripts/*.sh; do
    bash -n "$script"
done

if find results docs LICENSES -type f \
    \( -name '*.bin' -o -name '*.rom' -o -name '*.efi' -o \
       -name '*.EFI' -o -name '*.o' -o -name '*.so' -o -name '*.log' \) \
    -print -quit | grep -q .; then
    echo 'publication audit: binary/private artifact under public docs/results' >&2
    exit 1
fi

private_patterns=("$PWD" "$HOME")
if [[ -f config.local.mk ]]; then
    local_mount="$(sed -n 's/^USB_MOUNT[[:space:]]*:=[[:space:]]*//p' \
        config.local.mk | head -1)"
    local_serial="$(sed -n 's/^USB_SERIAL[[:space:]]*:=[[:space:]]*//p' \
        config.local.mk | head -1)"
    [[ -z "$local_mount" ]] || private_patterns+=("$local_mount")
    [[ -z "$local_serial" ]] || private_patterns+=("$local_serial")
fi
for pattern in "${private_patterns[@]}"; do
    [[ -z "$pattern" ]] && continue
    if rg -n -F -e "$pattern" \
        README.md README.ko.md LICENSE NOTICE.md Makefile config.local.mk.example \
        docs include LICENSES results scripts src tests tools; then
        echo 'publication audit: private path or USB identity leaked' >&2
        exit 1
    fi
done

for ignored in '/build/' '/artifacts/' '/references/' '/third_party/' \
    '/config.local.mk'; do
    grep -Fqx -- "$ignored" .gitignore
done

echo 'Publication audit: public results verified; JSON, links, shell syntax, privacy boundary, and ignore boundary passed'
