#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
# shellcheck source=scripts/lib/milan-stack-common.sh
source "$script_dir/lib/milan-stack-common.sh"

milan_require_root
milan_validate_prefix
milan_require_absolute "install root" "$MILAN_INSTALL_ROOT"
for command in flock timeout "$MILAN_SYSTEMCTL"; do
  milan_require_command "$command"
done

actual_prefix="$(milan_actual_prefix)"
actual_parent="$(dirname "$actual_prefix")"
actual_systemd_dir="$(milan_actual_systemd_dir)"
dropin="$actual_systemd_dir/$MILAN_DROPIN_NAME"
mkdir -p "$actual_parent"
exec 9>"$actual_parent/.goodix53x5-milan.install.lock"
flock -n 9 || milan_die "another Milan install/remove is active"

if [[ ! -e "$actual_prefix" && ! -e "$dropin" ]]; then
  milan_note "paired Milan shadow stack is already absent"
  exit 0
fi
[[ -e "$actual_prefix" && -e "$dropin" ]] || milan_die "refusing partial or unmanaged removal"
milan_verify_owned_marker "$actual_prefix"
expected_dropin="$actual_systemd_dir/.$MILAN_DROPIN_NAME.expected.$$"
milan_render_dropin "$repo_dir" "$expected_dropin"
cmp -s "$expected_dropin" "$dropin" || {
  rm -f -- "$expected_dropin"
  milan_die "refusing unmanaged or modified drop-in: $dropin"
}
rm -f -- "$expected_dropin"

tombstone="$actual_parent/.goodix53x5-milan.remove.$$"
saved_dropin="$actual_systemd_dir/.$MILAN_DROPIN_NAME.remove.$$"
[[ ! -e "$tombstone" && ! -e "$saved_dropin" ]] || milan_die "remove transaction path exists"
committed=0
rollback() {
  local rc=$?
  set +e
  if [[ "$committed" == 0 ]]; then
    [[ ! -e "$tombstone" ]] || mv "$tombstone" "$actual_prefix"
    [[ ! -e "$saved_dropin" ]] || mv "$saved_dropin" "$dropin"
    milan_systemctl daemon-reload >/dev/null 2>&1 || true
    milan_systemctl restart fprintd.service >/dev/null 2>&1 || true
  fi
  exit "$rc"
}
trap rollback EXIT

mv "$actual_prefix" "$tombstone"
mv "$dropin" "$saved_dropin"
milan_systemctl daemon-reload
milan_verify_packaged_selected
milan_systemctl restart fprintd.service
milan_verify_packaged_selected
committed=1
trap - EXIT

milan_verify_owned_marker "$tombstone"
milan_safe_remove_tree "$tombstone" "$actual_parent"
rm -f -- "$saved_dropin"
milan_note "removed only the owned Milan prefix and managed drop-in"
milan_note "packaged fprintd restarted; /var/lib/fprint was not touched"
