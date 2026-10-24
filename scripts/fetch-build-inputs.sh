#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
input_root="${1:-$repo_root/references}"
tmp_files=()
trap 'for path in "${tmp_files[@]:-}"; do rm -f -- "$path"; done' EXIT

for command_name in curl sha256sum mktemp; do
    command -v "$command_name" >/dev/null || {
        echo "missing required command: $command_name" >&2
        exit 2
    }
done

fetch_exact() {
    local url="$1" expected="$2" target="$3" actual tmp

    mkdir -p -- "$(dirname -- "$target")"
    if [[ -e "$target" ]]; then
        actual="$(sha256sum -- "$target" | awk '{print $1}')"
        if [[ "$actual" != "$expected" ]]; then
            echo "refusing mismatched existing input: $target" >&2
            exit 1
        fi
        printf 'verified existing %s\n' "$target"
        return
    fi

    tmp="$(mktemp "${target}.tmp.XXXXXX")"
    tmp_files+=("$tmp")
    curl --proto '=https' --tlsv1.2 --fail --location --silent \
        --show-error --output "$tmp" "$url"
    actual="$(sha256sum -- "$tmp" | awk '{print $1}')"
    if [[ "$actual" != "$expected" ]]; then
        echo "download hash mismatch for $url" >&2
        exit 1
    fi
    chmod 0644 "$tmp"
    mv -- "$tmp" "$target"
    printf 'fetched and verified %s\n' "$target"
}

nvidia_base='https://raw.githubusercontent.com/NVIDIA/open-gpu-kernel-modules/610.57.04/src/nvidia/generated'
nvidia_out="$input_root/NVIDIA-open-gpu-kernel-modules-610.57.04/src/nvidia/generated"
fetch_exact "$nvidia_base/g_bindata_kgspGetBinArchiveBooterLoadUcode_TU102.c" \
    e495483adf66798502667da137c4a24c55f471b86f3183020e3169ff88ad6c89 \
    "$nvidia_out/g_bindata_kgspGetBinArchiveBooterLoadUcode_TU102.c"
fetch_exact "$nvidia_base/g_bindata_ksec2GetBinArchiveBlUcode_TU102.c" \
    fc0bc277eeee754ea3071ae0fe8360a52193565bba7c07dcea60b00e8d56d9c8 \
    "$nvidia_out/g_bindata_ksec2GetBinArchiveBlUcode_TU102.c"
fetch_exact "$nvidia_base/g_bindata_kgspGetBinArchiveGspRmBoot_TU102.c" \
    19628e0b148a5437b351ce9ecd4a04405ec4eb66521e1c318ca6ef5d1a0c2efe \
    "$nvidia_out/g_bindata_kgspGetBinArchiveGspRmBoot_TU102.c"

xrip_commit='df2fe11b370bb52cc314576d0f2f5ae47ce908f8'
xrip_base="https://raw.githubusercontent.com/xrip/cmp50hx-unlock/$xrip_commit"
xrip_out="$input_root/xrip__cmp50hx-unlock"
fetch_exact "$xrip_base/efi-unlock/blobs/v67_payload.bin" \
    de7288bcfb80c2d4b259a59e823c926ba0a9adcab594219cc6802c94257ce271 \
    "$xrip_out/efi-unlock/blobs/v67_payload.bin"
fetch_exact "$xrip_base/efi-unlock/blobs/gsp_rm_boot_dbg.bin" \
    fa43239bcee7b97ca62f007cc68487560a39e19f74f3dde7486db3f98df8e471 \
    "$xrip_out/efi-unlock/blobs/gsp_rm_boot_dbg.bin"
fetch_exact "$xrip_base/LICENSE" \
    022cd683fdf2b2a3566501e75e3036c44b75283ebeab2e058e67cd7c0ced9998 \
    "$xrip_out/LICENSE"
fetch_exact "$xrip_base/efi-unlock/LICENSE-40HX-UNLOCK" \
    75054b6a08e1008c86b6045ab99add2c8fa571987880951a9929f11ee56a7d64 \
    "$xrip_out/efi-unlock/LICENSE-40HX-UNLOCK"

echo 'All external build inputs are present and hash-verified.'
