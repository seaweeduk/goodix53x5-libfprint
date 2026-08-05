#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
# shellcheck source=scripts/lib/milan-stack-common.sh
source "$script_dir/lib/milan-stack-common.sh"

mode="${1:---installed}"
milan_validate_prefix
milan_reject_ephemeral_root "GOODIX_MILAN_STACK_ROOT" "$MILAN_STACK_ROOT"
milan_require_command timeout

verify_build() {
  local current="$MILAN_STACK_ROOT/builds/current" prefix

  [[ -L "$current" ]] || milan_die "no published stack at $current"
  prefix="$(milan_root_path "$current/payload" "$MILAN_PREFIX")"
  milan_verify_manifest "$prefix" "$repo_dir"
  milan_note "build payload verified: $(readlink -f "$current")"
}

verify_destination_guards() {
  local actual_prefix actual_systemd_dir dropin udev_rule

  actual_prefix="$(milan_actual_prefix)"
  actual_systemd_dir="$(milan_actual_systemd_dir)"
  dropin="$actual_systemd_dir/$MILAN_DROPIN_NAME"
  udev_rule="$(milan_actual_udev_dir)/$MILAN_UDEV_RULE_NAME"
  [[ ! -e "$actual_prefix" ]] || milan_verify_owned_marker "$actual_prefix"
  if [[ -e "$dropin" ]]; then
    [[ -e "$actual_prefix" ]] || milan_die "unmanaged drop-in without owned prefix"
    milan_render_dropin "$repo_dir" | cmp -s - "$dropin" ||
      milan_die "unmanaged or modified drop-in"
  fi
  if [[ -e "$udev_rule" ]]; then
    [[ -e "$actual_prefix" ]] || milan_die "unmanaged USB persistence rule without owned prefix"
    cmp -s "$actual_prefix/share/udev/rules.d/$MILAN_UDEV_RULE_NAME" "$udev_rule" ||
      milan_die "unmanaged or modified USB persistence rule"
  fi
}

case "$mode" in
  --build)
    verify_build
    ;;
  --preflight)
    verify_build
    verify_destination_guards
    milan_verify_state_directory
    milan_note "install preflight passed"
    ;;
  --installed)
    actual_prefix="$(milan_actual_prefix)"
    dropin="$(milan_actual_systemd_dir)/$MILAN_DROPIN_NAME"
    udev_rule="$(milan_actual_udev_dir)/$MILAN_UDEV_RULE_NAME"
    milan_verify_owned_marker "$actual_prefix"
    milan_verify_manifest "$actual_prefix" "$repo_dir"
    [[ -f "$dropin" ]] || milan_die "managed drop-in is missing"
    milan_render_dropin "$repo_dir" | cmp -s - "$dropin" ||
      milan_die "managed drop-in differs from template"
    [[ -f "$udev_rule" ]] || milan_die "managed USB persistence rule is missing"
    cmp -s "$actual_prefix/share/udev/rules.d/$MILAN_UDEV_RULE_NAME" "$udev_rule" ||
      milan_die "managed USB persistence rule differs from payload"
    milan_verify_active_shadow
    milan_verify_usb_persist
    milan_note "installed shadow stack and service configuration verified"
    ;;
  --packaged)
    milan_verify_state_directory
    milan_verify_packaged_selected
    milan_note "packaged fprintd is selected"
    ;;
  *) milan_die "usage: $0 [--build|--preflight|--installed|--packaged]" ;;
esac
