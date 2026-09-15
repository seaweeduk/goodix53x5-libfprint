#!/usr/bin/env bash
# Install a separately built publication into normal system paths.
set -euo pipefail
umask 022

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
source "$script_dir/lib/milan-stack-common.sh"

milan_require_root
milan_reject_ephemeral_root "GOODIX_MILAN_STACK_ROOT" "$MILAN_STACK_ROOT"
for command in python3 flock timeout udevadm systemctl ldconfig; do
  milan_require_command "$command"
done

# The builder holds this lock exclusively while publishing/pruning builds.
# Keep it distinct from fd 9, the system installation lock.
exec 8<"$MILAN_STACK_ROOT/.build.lock"
flock -s 8
current="$MILAN_STACK_ROOT/builds/current"
[[ -L "$current" ]] || milan_die "no published stack; run build-milan-stack-local.sh"
payload="$(readlink -f "$current")/payload"
milan_verify_manifest "$payload" "$repo_dir"
# All conflicts, including the old installation and package ownership, are
# checked before creating the lock or changing files or service state.
milan_files preflight "$MILAN_INSTALL_ROOT" "$payload"
milan_check_admin_mask
milan_lock_install
milan_files preflight "$MILAN_INSTALL_ROOT" "$payload"
milan_check_admin_mask

# A runtime mask can hide the unit's properties. UAT supplies the writer's
# exact dump directory when handing over an already-masked service.
capture_environment="$(milan_systemctl show fprintd.service --property=Environment --value --no-pager)"
capture_dir="${GOODIX_MILAN_CAPTURE_DIR-$(milan_capture_directory "$capture_environment")}"
if [[ -v GOODIX_MILAN_CAPTURE_DIR || -n "$capture_dir" ]]; then
  milan_require_absolute "capture dump directory" "$capture_dir"
  [[ "$capture_dir" != *[[:space:]]* ]] || milan_die "capture dump directory must not contain whitespace"
  [[ -d "$(dirname "$capture_dir")" ]] || milan_die "capture campaign directory does not exist"
  [[ "$(milan_manifest_value "$payload$MILAN_BUILD_ENV" GOODIX53X5_DEBUG)" == 1 ]] ||
    milan_die "a configured capture writer requires a debug build"
fi

record_capture_build_manifest() {
  local campaign_dir candidate debug dump_dir manifest
  local manifest_user tool library
  debug="$(milan_manifest_value "$(milan_root_path "$MILAN_INSTALL_ROOT" "$MILAN_BUILD_ENV")" GOODIX53X5_DEBUG)"
  [[ "$debug" == 1 ]] || return 0
  dump_dir="$capture_dir"
  [[ -n "$dump_dir" ]] || return 0
  milan_require_absolute "capture dump directory" "$dump_dir"
  campaign_dir="$(dirname "$dump_dir")"
  [[ -d "$campaign_dir" ]] || milan_die "capture campaign directory does not exist: $campaign_dir"
  tool="$repo_dir/tools/milan-parity/milan-parity"
  manifest="$campaign_dir/driver-build.json"
  candidate="$campaign_dir/.driver-build.json.$$"
  [[ ! -e "$candidate" && ! -L "$candidate" ]] || milan_die "capture manifest candidate exists: $candidate"
  library="$(milan_root_path "$MILAN_INSTALL_ROOT" "$MILAN_LIBRARY_PATH")"
  manifest_user="${SUDO_USER:-root}"
  if [[ "$manifest_user" != root ]]; then
    milan_require_command runuser
    runuser -u "$manifest_user" -- "$tool" build-manifest --repo "$repo_dir" \
      --library "$library" --output "$candidate" --debug
  else
    "$tool" build-manifest --repo "$repo_dir" --library "$library" --output "$candidate" --debug
  fi
  if [[ -e "$manifest" ]]; then
    if ! cmp -s "$candidate" "$manifest"; then
      rm -f -- "$candidate"
      milan_die "capture campaign already records a different build: $manifest"
    fi
    rm -f -- "$candidate"
  else
    mv "$candidate" "$manifest"
  fi
  milan_note "capture build manifest verified at $manifest"
}

install_failed() {
  local rc=$?
  trap - EXIT
  if (( rc != 0 )); then
    if milan_mask_runtime; then
      printf 'error: install incomplete; fprintd remains runtime-masked. Inspect files and retry; pass GOODIX_MILAN_CAPTURE_DIR for a configured capture writer.\n' >&2
    else
      printf 'error: install incomplete and runtime masking failed; keep fprintd stopped while inspecting the installation.\n' >&2
    fi
  fi
  exit "$rc"
}
trap install_failed EXIT
# Stopping alone permits D-Bus to reactivate a partially replaced pair.
milan_mask_runtime
milan_files install "$MILAN_INSTALL_ROOT" "$payload"
milan_restore_labels
milan_refresh_linker
udevadm control --reload-rules
milan_systemctl daemon-reload
milan_load_layout "$MILAN_INSTALL_ROOT"
milan_verify_ldd
# Seal the installed bytes before starting a configured capture writer.
record_capture_build_manifest
milan_apply_usb_persist 1
milan_verify_usb_persist
milan_unmask_runtime
milan_verify_active_system
actual_environment="$(milan_systemctl show fprintd.service --property=Environment --value --no-pager)"
[[ "$(milan_capture_directory "$actual_environment")" == "$capture_dir" ]] ||
  milan_die "service capture directory differs from the sealed writer; retry with its exact GOODIX_MILAN_CAPTURE_DIR"
milan_systemctl restart fprintd.service
trap - EXIT
milan_note "installed paired Milan libfprint and full fprintd runtime in /usr"
milan_note "preserved /var/lib/fprint and the imported Windows PSK"
