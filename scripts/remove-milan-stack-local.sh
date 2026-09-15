#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/lib/milan-stack-common.sh"
milan_require_root
for command in python3 flock timeout udevadm systemctl ldconfig; do
  milan_require_command "$command"
done

metadata="$(milan_root_path "$MILAN_INSTALL_ROOT" "$MILAN_METADATA_DIR")"
installed=1
if [[ ! -e "$metadata/ownership.json" && ! -L "$metadata/ownership.json" ]]; then
  [[ ! -e "$metadata/build.env" && ! -e "$metadata/inventory.json" && ! -e "$metadata/SHA256SUMS" ]] ||
    milan_die "installation metadata exists without ownership; inspect partial installation"
  if [[ ! -e "$(milan_runtime_marker)" && ! -L "$(milan_runtime_marker)" ]]; then
    milan_note "manual Milan installation is already absent"
    exit 0
  fi
  installed=0
fi
# Check every owned file and package takeover before stopping the daemon.
if [[ "$installed" == 1 ]]; then
  milan_files verify-installed "$MILAN_INSTALL_ROOT"
  milan_load_layout "$MILAN_INSTALL_ROOT"
fi
milan_lock_install
# Recheck after locking, including an installation completed while we waited.
if [[ -e "$metadata/ownership.json" || -L "$metadata/ownership.json" ]]; then
  milan_files verify-installed "$MILAN_INSTALL_ROOT"
  milan_mask_runtime
  milan_files remove "$MILAN_INSTALL_ROOT" ||
    milan_die "file removal incomplete; fprintd remains runtime-masked while you inspect the recorded files"
else
  [[ ! -e "$metadata/build.env" && ! -e "$metadata/inventory.json" && ! -e "$metadata/SHA256SUMS" ]] ||
    milan_die "partial installation metadata remains"
fi
# Each cleanup is independent; an absent retry must complete all of them too.
cleanup_failed=0
(milan_refresh_linker) || { printf 'error: linker-cache cleanup failed\n' >&2; cleanup_failed=1; }
udevadm control --reload-rules || { printf 'error: udev reload failed\n' >&2; cleanup_failed=1; }
(milan_apply_usb_persist 0) || { printf 'error: USB persistence cleanup failed\n' >&2; cleanup_failed=1; }
milan_systemctl daemon-reload || { printf 'error: daemon reload failed\n' >&2; cleanup_failed=1; }
[[ "$cleanup_failed" == 0 ]] || milan_die "removal cleanup incomplete; retry uninstall (the maintenance mask is retained)"
if [[ -f "$(milan_runtime_marker)" && ! -L "$(milan_runtime_marker)" ]]; then
  if ! milan_unmask_runtime; then
    milan_mask_runtime || true
    milan_die "could not clear the temporary runtime mask; retry uninstall"
  fi
fi
milan_note "removed only recorded unchanged Milan files; no daemon was restarted"
milan_note "preserved /var/lib/fprint and the imported Windows PSK"
