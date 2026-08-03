#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
# shellcheck source=scripts/lib/milan-stack-common.sh
source "$script_dir/lib/milan-stack-common.sh"

milan_require_root
milan_validate_prefix
milan_reject_ephemeral_root "GOODIX_MILAN_STACK_ROOT" "$MILAN_STACK_ROOT"
milan_require_absolute "install root" "$MILAN_INSTALL_ROOT"
for command in flock timeout "$MILAN_SYSTEMCTL"; do
  milan_require_command "$command"
done

current="$MILAN_STACK_ROOT/builds/current"
[[ -L "$current" ]] || milan_die "no published stack; run build-milan-stack-local.sh"
payload_prefix="$(milan_root_path "$current/payload" "$MILAN_PREFIX")"
milan_verify_manifest "$payload_prefix" "$repo_dir"
milan_verify_state_directory

actual_prefix="$(milan_actual_prefix)"
actual_parent="$(dirname "$actual_prefix")"
actual_systemd_dir="$(milan_actual_systemd_dir)"
dropin="$actual_systemd_dir/$MILAN_DROPIN_NAME"
mkdir -p "$actual_parent" "$actual_systemd_dir"
exec 9>"$actual_parent/.goodix53x5-milan.install.lock"
flock -n 9 || milan_die "another Milan install/remove is active"

expected_dropin="$MILAN_STACK_ROOT/.install-dropin.$$"
milan_render_dropin "$repo_dir" "$expected_dropin"
trap 'rm -f -- "$expected_dropin"' EXIT
if [[ -e "$actual_prefix" ]]; then
  milan_verify_owned_marker "$actual_prefix"
fi
if [[ -e "$dropin" ]]; then
  [[ -e "$actual_prefix" ]] || milan_die "refusing unmanaged drop-in without owned prefix: $dropin"
  cmp -s "$expected_dropin" "$dropin" || milan_die "refusing unmanaged or modified drop-in: $dropin"
fi

if [[ -e "$actual_prefix" && -e "$dropin" ]] &&
   cmp -s "$payload_prefix/manifest/build.env" "$actual_prefix/manifest/build.env" &&
   cmp -s "$expected_dropin" "$dropin"; then
  rm -f -- "$expected_dropin"
  trap - EXIT
  "$script_dir/status-milan-stack-local.sh" --installed
  milan_note "paired Milan stack is already installed"
  exit 0
fi

stage_prefix="$actual_parent/.goodix53x5-milan.stage.$$"
backup_prefix="$actual_parent/.goodix53x5-milan.backup.$$"
stage_dropin="$actual_systemd_dir/.$MILAN_DROPIN_NAME.stage.$$"
backup_dropin="$actual_systemd_dir/.$MILAN_DROPIN_NAME.backup.$$"
for path in "$stage_prefix" "$backup_prefix" "$stage_dropin" "$backup_dropin"; do
  [[ ! -e "$path" ]] || milan_die "transaction path already exists: $path"
done

mkdir "$stage_prefix"
cp -a "$payload_prefix/." "$stage_prefix/"
cat > "$stage_prefix/$MILAN_OWNED_MARKER" <<EOF
FORMAT=1
MANAGED_BY=goodix53x5-milan-stack
PREFIX=$MILAN_PREFIX
BUILD_MANIFEST_SHA256=$(milan_sha256 "$stage_prefix/manifest/build.env")
EOF
chown -hR "$(id -u):$(id -g)" -- "$stage_prefix"
chmod -R u-s,g-s,go-w -- "$stage_prefix"
cp "$expected_dropin" "$stage_dropin"
chmod 0644 "$stage_dropin"
rm -f -- "$expected_dropin"
milan_verify_owned_marker "$stage_prefix"
milan_verify_manifest "$stage_prefix" "$repo_dir"

transaction_started=0
committed=0
old_prefix_moved=0
new_prefix_active=0
old_dropin_moved=0
new_dropin_active=0
rollback() {
  local rc=$?
  set +e
  if [[ "$transaction_started" == 1 && "$committed" == 0 ]]; then
    if [[ "$new_prefix_active" == 1 && -e "$actual_prefix" ]]; then
      milan_verify_owned_marker "$actual_prefix" >/dev/null 2>&1 &&
        milan_safe_remove_tree "$actual_prefix" "$actual_parent"
    fi
    [[ "$old_prefix_moved" == 0 || ! -e "$backup_prefix" ]] || mv "$backup_prefix" "$actual_prefix"
    [[ "$new_dropin_active" == 0 ]] || rm -f -- "$dropin"
    [[ "$old_dropin_moved" == 0 || ! -e "$backup_dropin" ]] || mv "$backup_dropin" "$dropin"
    milan_systemctl daemon-reload >/dev/null 2>&1 || true
    milan_systemctl restart fprintd.service >/dev/null 2>&1 || true
  fi
  [[ ! -e "$stage_prefix" ]] || milan_safe_remove_tree "$stage_prefix" "$actual_parent"
  rm -f -- "$stage_dropin" "$expected_dropin"
  exit "$rc"
}
trap rollback EXIT

# Verification is complete; the service is stopped only for the atomic switch.
milan_systemctl stop fprintd.service
transaction_started=1
if [[ -e "$actual_prefix" ]]; then
  mv "$actual_prefix" "$backup_prefix"
  old_prefix_moved=1
fi
mv "$stage_prefix" "$actual_prefix"
new_prefix_active=1
if [[ -e "$dropin" ]]; then
  mv "$dropin" "$backup_dropin"
  old_dropin_moved=1
fi
mv "$stage_dropin" "$dropin"
new_dropin_active=1
milan_verify_manifest "$actual_prefix" "$repo_dir"
milan_systemctl daemon-reload
milan_systemctl restart fprintd.service
milan_verify_active_shadow
committed=1
trap - EXIT

[[ ! -e "$backup_prefix" ]] || milan_safe_remove_tree "$backup_prefix" "$actual_parent"
rm -f -- "$backup_dropin"
milan_note "installed paired Milan shadow stack under $MILAN_PREFIX"
milan_note "preserved StateDirectory=fprint and /var/lib/fprint"
