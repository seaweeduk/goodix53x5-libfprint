#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
source "$script_dir/lib/milan-stack-common.sh"
[[ "$#" -le 1 ]] || milan_die "expected one status mode"
mode="${1:---installed}"

case "$mode" in
  --build|--preflight)
    milan_reject_ephemeral_root "GOODIX_MILAN_STACK_ROOT" "$MILAN_STACK_ROOT"
    current="$MILAN_STACK_ROOT/builds/current"
    [[ -L "$current" ]] || milan_die "no published stack at $current"
    payload="$(readlink -f "$current")/payload"
    milan_verify_manifest "$payload" "$repo_dir"
    milan_note "build payload verified: $(readlink -f "$current")"
    if [[ "$mode" == --preflight ]]; then
      milan_files preflight "$MILAN_INSTALL_ROOT" "$payload"
      milan_note "install preflight passed"
    fi
    ;;
  --installed)
    milan_files verify-installed "$MILAN_INSTALL_ROOT"
    milan_load_layout "$MILAN_INSTALL_ROOT"
    milan_verify_debug_census "$MILAN_INSTALL_ROOT" \
      "$(milan_manifest_value "$(milan_root_path "$MILAN_INSTALL_ROOT" "$MILAN_BUILD_ENV")" GOODIX53X5_DEBUG)"
    milan_verify_active_system
    milan_verify_usb_persist
    milan_note "owned installed files, system service, and resolved library verified"
    ;;
  --absent)
    for name in build.env inventory.json SHA256SUMS ownership.json; do
      path="$(milan_root_path "$MILAN_INSTALL_ROOT" "$MILAN_METADATA_DIR/$name")"
      [[ ! -e "$path" && ! -L "$path" ]] || milan_die "manual installation metadata remains: $path"
    done
    milan_note "manual Milan installation is absent"
    ;;
  *) milan_die "usage: $0 [--build|--preflight|--installed|--absent]" ;;
esac
