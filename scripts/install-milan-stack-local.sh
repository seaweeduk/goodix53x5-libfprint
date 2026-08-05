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
for command in flock timeout udevadm "$MILAN_SYSTEMCTL"; do
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
actual_udev_dir="$(milan_actual_udev_dir)"
udev_rule="$actual_udev_dir/$MILAN_UDEV_RULE_NAME"
payload_udev_rule="$payload_prefix/share/udev/rules.d/$MILAN_UDEV_RULE_NAME"
[[ -f "$payload_udev_rule" ]] || milan_die "payload USB persistence rule is missing"
mkdir -p "$actual_parent" "$actual_systemd_dir" "$actual_udev_dir"
exec 9>"$actual_parent/.goodix53x5-milan.install.lock"
flock -n 9 || milan_die "another Milan install/remove is active"

record_capture_build_manifest() {
  local assignment campaign_dir candidate debug dump_dir environment manifest
  local manifest_user tool

  debug="$(milan_manifest_value "$actual_prefix/manifest/build.env" GOODIX53X5_DEBUG)"
  [[ "$debug" == 1 ]] || return 0
  if ! environment="$(milan_systemctl show fprintd.service --property=Environment --value --no-pager 2>/dev/null)"; then
    return 0
  fi
  dump_dir=""
  for assignment in $environment; do
    case "$assignment" in
      GOODIX53X5_DUMP_DIR=*) dump_dir="${assignment#GOODIX53X5_DUMP_DIR=}" ;;
    esac
  done
  [[ -n "$dump_dir" ]] || return 0
  milan_require_absolute "capture dump directory" "$dump_dir"
  campaign_dir="$(dirname "$dump_dir")"
  [[ -d "$campaign_dir" ]] || milan_die "capture campaign directory does not exist: $campaign_dir"
  tool="$repo_dir/tools/milan-parity/milan-parity"
  [[ -x "$tool" ]] || milan_die "Milan parity tool is unavailable: $tool"
  manifest="$campaign_dir/driver-build.json"
  candidate="$campaign_dir/.driver-build.json.$$"
  [[ ! -e "$candidate" ]] || milan_die "capture manifest candidate already exists: $candidate"
  manifest_user="${SUDO_USER:-root}"
  if [[ "$manifest_user" != root ]]; then
    milan_require_command runuser
    runuser -u "$manifest_user" -- "$tool" build-manifest \
      --repo "$repo_dir" \
      --library "$actual_prefix/lib/libfprint-2.so.2.0.0" \
      --output "$candidate" --debug
  else
    "$tool" build-manifest --repo "$repo_dir" \
      --library "$actual_prefix/lib/libfprint-2.so.2.0.0" \
      --output "$candidate" --debug
  fi
  if [[ -e "$manifest" ]]; then
    if ! cmp -s "$candidate" "$manifest"; then
      rm -f -- "$candidate"
      milan_die "capture campaign already records a different build: $manifest"
    fi
    rm -f -- "$candidate"
    milan_note "capture build manifest already matches $manifest"
  else
    mv "$candidate" "$manifest"
    milan_note "recorded capture build manifest at $manifest"
  fi
}

if [[ -e "$actual_prefix" ]]; then
  milan_verify_owned_marker "$actual_prefix"
fi
if [[ -e "$dropin" ]]; then
  [[ -e "$actual_prefix" ]] || milan_die "refusing unmanaged drop-in without owned prefix: $dropin"
  milan_render_dropin "$repo_dir" | cmp -s - "$dropin" || milan_die "refusing unmanaged or modified drop-in: $dropin"
fi
if [[ -e "$udev_rule" ]]; then
  [[ -e "$actual_prefix" ]] || milan_die "refusing unmanaged USB persistence rule: $udev_rule"
  cmp -s "$payload_udev_rule" "$udev_rule" || milan_die "refusing unmanaged or modified USB persistence rule: $udev_rule"
fi

if [[ -e "$actual_prefix" && -e "$dropin" && -e "$udev_rule" ]] &&
   cmp -s "$payload_prefix/manifest/build.env" "$actual_prefix/manifest/build.env" &&
   milan_render_dropin "$repo_dir" | cmp -s - "$dropin" &&
   cmp -s "$payload_udev_rule" "$udev_rule"; then
  "$script_dir/status-milan-stack-local.sh" --installed
  record_capture_build_manifest
  milan_note "paired Milan stack is already installed"
  exit 0
fi

stage_prefix="$actual_parent/.goodix53x5-milan.stage.$$"
backup_prefix="$actual_parent/.goodix53x5-milan.backup.$$"
stage_dropin="$actual_systemd_dir/.$MILAN_DROPIN_NAME.stage.$$"
backup_dropin="$actual_systemd_dir/.$MILAN_DROPIN_NAME.backup.$$"
stage_udev_rule="$actual_udev_dir/.$MILAN_UDEV_RULE_NAME.stage.$$"
backup_udev_rule="$actual_udev_dir/.$MILAN_UDEV_RULE_NAME.backup.$$"
for path in "$stage_prefix" "$backup_prefix" "$stage_dropin" "$backup_dropin" \
            "$stage_udev_rule" "$backup_udev_rule"; do
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
milan_render_dropin "$repo_dir" > "$stage_dropin"
chmod 0644 "$stage_dropin"
cp "$stage_prefix/share/udev/rules.d/$MILAN_UDEV_RULE_NAME" "$stage_udev_rule"
chmod 0644 "$stage_udev_rule"
milan_verify_owned_marker "$stage_prefix"
milan_verify_manifest "$stage_prefix" "$repo_dir"

transaction_started=0
committed=0
old_prefix_moved=0
new_prefix_active=0
old_dropin_moved=0
new_dropin_active=0
old_udev_rule_moved=0
new_udev_rule_active=0
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
    [[ "$new_udev_rule_active" == 0 ]] || rm -f -- "$udev_rule"
    [[ "$old_udev_rule_moved" == 0 || ! -e "$backup_udev_rule" ]] || mv "$backup_udev_rule" "$udev_rule"
    udevadm control --reload-rules >/dev/null 2>&1 || true
    if [[ -e "$udev_rule" ]]; then
      milan_apply_usb_persist 1 >/dev/null 2>&1 || true
    else
      milan_apply_usb_persist 0 >/dev/null 2>&1 || true
    fi
    milan_systemctl daemon-reload >/dev/null 2>&1 || true
    milan_systemctl restart fprintd.service >/dev/null 2>&1 || true
  fi
  [[ ! -e "$stage_prefix" ]] || milan_safe_remove_tree "$stage_prefix" "$actual_parent"
  rm -f -- "$stage_dropin" "$stage_udev_rule"
  exit "$rc"
}
trap rollback EXIT

# Verification is complete; the service is stopped only for the atomic switch.
fprintd_was_active=0
if milan_systemctl is-active --quiet fprintd.service; then
  fprintd_was_active=1
fi
if milan_systemctl stop fprintd.service; then
  transaction_started=1
else
  stop_rc=$?
  if [[ "$fprintd_was_active" == 1 ]] &&
     ! milan_systemctl is-active --quiet fprintd.service; then
    milan_systemctl start fprintd.service >/dev/null 2>&1 || true
  fi
  exit "$stop_rc"
fi
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
if [[ -e "$udev_rule" ]]; then
  mv "$udev_rule" "$backup_udev_rule"
  old_udev_rule_moved=1
fi
mv "$stage_udev_rule" "$udev_rule"
new_udev_rule_active=1
milan_verify_manifest "$actual_prefix" "$repo_dir"
udevadm control --reload-rules
milan_systemctl daemon-reload
milan_systemctl restart fprintd.service
milan_verify_active_shadow
milan_apply_usb_persist 1
milan_verify_usb_persist
committed=1
trap - EXIT

[[ ! -e "$backup_prefix" ]] || milan_safe_remove_tree "$backup_prefix" "$actual_parent"
rm -f -- "$backup_dropin" "$backup_udev_rule"
record_capture_build_manifest
milan_note "installed paired Milan shadow stack under $MILAN_PREFIX"
milan_note "enabled hibernate USB persistence for supported Milan sensors"
milan_note "preserved StateDirectory=fprint and /var/lib/fprint"
