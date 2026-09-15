#!/usr/bin/env bash

MILAN_LIBFPRINT_REVISION="0c97a47d8ef405cd577b87058c1e89cae9d242e7"
MILAN_FPRINTD_REVISION="b54a007ccf58ac0ae074c7151b223f35cbd17306"
MILAN_LIBFPRINT_SOURCE_TREE="2d08bc33d953cd17b315c5f5199aa7a0d0504506"
MILAN_FPRINTD_SOURCE_TREE="ff82f8c3c2ab936ddafec9e88e650c04cd6f4f1d"
MILAN_LIBFPRINT_PATCH_SHA256="fa9a4a89df02894a01013dc787d06cdbb74a4908b8e3cdc5da745e0265fb2f72"
MILAN_LIBFPRINT_USB_PERSIST_PATCH_SHA256="743c13782228869b8b5ea834caa096abadd38e5542303d7af8bc7acb4c925ae0"
MILAN_LIBFPRINT_IDLE_SUSPEND_NOTIFY_PATCH_SHA256="ec357fef155b2b6a0be6d91e697e4c4cbd3f5f4cec5218cd95b94008d7e7e548"
MILAN_FPRINTD_PATCH_SHA256="5d87cd806587fa5f035847a38ba3155b38f9a612a3070d9abfa6f83114e58db8"
MILAN_PREFIX="/usr"
MILAN_METADATA_DIR="/usr/share/goodix53x5-milan"
MILAN_BUILD_ENV="$MILAN_METADATA_DIR/build.env"
MILAN_UDEV_DIR="/usr/lib/udev/rules.d"
MILAN_UDEV_RULE_NAME="99-goodix53x5-milan-persist.rules"
MILAN_OWNED_MARKER="$MILAN_METADATA_DIR/ownership.json"
MILAN_FILES_HELPER="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/milan-stack-files.py"

milan_die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

milan_note() {
  printf '%s\n' "$*"
}

milan_require_command() {
  command -v "$1" >/dev/null 2>&1 || milan_die "required command not found: $1"
}

milan_require_absolute() {
  case "$2" in
    /*) ;;
    *) milan_die "$1 must be an absolute path: $2" ;;
  esac
}

milan_reject_ephemeral_root() {
  milan_require_absolute "$1" "$2"
  case "$2/" in
    /tmp/|/tmp/*|/var/tmp/|/var/tmp/*|/run/|/run/*)
      milan_die "$1 must be persistent, not $2"
      ;;
  esac
}

milan_invoking_home() {
  local entry

  if [[ "${EUID:-$(id -u)}" == 0 && -n "${SUDO_USER:-}" && "$SUDO_USER" != root ]]; then
    milan_require_command getent
    entry="$(getent passwd "$SUDO_USER")"
    [[ -n "$entry" ]] || milan_die "cannot resolve home for $SUDO_USER"
    printf '%s\n' "$(cut -d: -f6 <<<"$entry")"
  else
    [[ -n "${HOME:-}" ]] || milan_die "HOME is not set"
    printf '%s\n' "$HOME"
  fi
}

milan_default_stack_root() {
  local home state_home

  home="$(milan_invoking_home)"
  if [[ "${EUID:-$(id -u)}" != 0 && -n "${XDG_STATE_HOME:-}" ]]; then
    state_home="$XDG_STATE_HOME"
  else
    state_home="$home/.local/state"
  fi
  printf '%s\n' "$state_home/goodix53x5-milan"
}

MILAN_STACK_ROOT="${GOODIX_MILAN_STACK_ROOT:-$(milan_default_stack_root)}"
MILAN_INSTALL_ROOT=/

milan_detect_layout() {
  local ID= ID_LIKE= multiarch
  [[ ! -r /etc/os-release ]] || source /etc/os-release
  MILAN_LIBDIR=/usr/lib
  MILAN_DAEMON_PATH=/usr/libexec/fprintd
  case " $ID $ID_LIKE " in
    *" arch "*) MILAN_DAEMON_PATH=/usr/lib/fprintd ;;
    *" debian "*|*" ubuntu "*)
      milan_require_command dpkg-architecture
      multiarch="$(dpkg-architecture -qDEB_HOST_MULTIARCH)"
      [[ "$multiarch" =~ ^[a-zA-Z0-9_-]+$ ]] || milan_die "invalid Debian multiarch tuple"
      MILAN_LIBDIR="/usr/lib/$multiarch" ;;
    *" fedora "*|*" rhel "*|*" centos "*|*" suse "*)
      milan_require_command rpm
      MILAN_LIBDIR="$(rpm --eval '%{_libdir}')" ;;
  esac
  MILAN_LIBRARY_PATH="$MILAN_LIBDIR/libfprint-2.so.2.0.0"
}

milan_root_path() {
  local root="$1" absolute="$2"

  milan_require_absolute "mapped path" "$absolute"
  if [[ "$root" == / ]]; then
    printf '%s\n' "$absolute"
  else
    printf '%s%s\n' "${root%/}" "$absolute"
  fi
}

milan_sha256() {
  sha256sum "$1" | cut -d ' ' -f 1
}

milan_overlay_input_sha256() {
  local repo_dir="$1"

  (
    cd "$repo_dir"
    find drivers/goodix53x5 tests udev -type f -print0 |
      LC_ALL=C sort -z |
      xargs -0 sha256sum
    sha256sum meson-integration.patch scripts/build-local.sh \
      patches/libfprint/libfprint-update-result.patch \
      patches/libfprint/libfprint-goodix53x5-usb-persist.patch \
      patches/libfprint/libfprint-idle-suspend-notify.patch \
      patches/fprintd/1.94.5-milan-update-save.patch
  ) | sha256sum | cut -d ' ' -f 1
}

milan_manifest_value() {
  local manifest="$1" key="$2" line

  line="$(grep -E "^${key}=" "$manifest" || true)"
  [[ -n "$line" && "$line" != *$'\n'* ]] || milan_die "missing or duplicate $key in $manifest"
  printf '%s\n' "${line#*=}"
}

milan_verify_repo_inputs() {
  local repo_dir="$1"
  local lib_patch="$repo_dir/patches/libfprint/libfprint-update-result.patch"
  local persist_patch="$repo_dir/patches/libfprint/libfprint-goodix53x5-usb-persist.patch"
  local idle_suspend_patch="$repo_dir/patches/libfprint/libfprint-idle-suspend-notify.patch"
  local daemon_patch="$repo_dir/patches/fprintd/1.94.5-milan-update-save.patch"

  [[ "$(milan_sha256 "$lib_patch")" == "$MILAN_LIBFPRINT_PATCH_SHA256" ]] ||
    milan_die "libfprint patch digest mismatch"
  [[ "$(milan_sha256 "$persist_patch")" == "$MILAN_LIBFPRINT_USB_PERSIST_PATCH_SHA256" ]] ||
    milan_die "libfprint USB persist patch digest mismatch"
  [[ "$(milan_sha256 "$idle_suspend_patch")" == "$MILAN_LIBFPRINT_IDLE_SUSPEND_NOTIFY_PATCH_SHA256" ]] ||
    milan_die "libfprint idle suspend notification patch digest mismatch"
  [[ "$(milan_sha256 "$daemon_patch")" == "$MILAN_FPRINTD_PATCH_SHA256" ]] ||
    milan_die "fprintd patch digest mismatch"
  (cd "$(dirname "$daemon_patch")" && sha256sum --check "$(basename "$daemon_patch").sha256" >/dev/null) ||
    milan_die "fprintd patch sidecar verification failed"
}

milan_verify_git_pristine() {
  local source_dir="$1" expected="$2" label="$3" actual

  [[ -d "$source_dir/.git" ]] || milan_die "$label is not a git checkout: $source_dir"
  actual="$(git -C "$source_dir" rev-parse HEAD)"
  [[ "$actual" == "$expected" ]] || milan_die "$label revision mismatch: expected $expected, got $actual"
  [[ -z "$(git -C "$source_dir" status --porcelain --untracked-files=all)" ]] ||
    milan_die "$label checkout is dirty: $source_dir"
}

milan_run_stage() {
  local label="$1"
  shift
  milan_note "==> $label"
  timeout --foreground 300 "$@"
}

milan_verify_ldd() {
  local daemon output resolved
  daemon="$(milan_root_path "$MILAN_INSTALL_ROOT" "$MILAN_DAEMON_PATH")"
  output="$(timeout --foreground 300 env -u LD_LIBRARY_PATH -u LD_PRELOAD ldd "$daemon")" ||
    milan_die "ldd failed for installed daemon"
  resolved="$(awk '$1 == "libfprint-2.so.2" && $2 == "=>" {print $3}' <<<"$output")"
  [[ -n "$resolved" && "$resolved" != *$'\n'* &&
     "$(readlink -f "$resolved")" == "$(readlink -f "$(milan_root_path "$MILAN_INSTALL_ROOT" "$MILAN_LIBRARY_PATH")")" ]] ||
    milan_die "daemon resolves wrong libfprint: $resolved"
  [[ "$output" != *"not found"* ]] || milan_die "daemon has unresolved dependencies"
}

milan_verify_debug_census() {
  local expected="$2" library
  library="$(milan_root_path "$1" "$MILAN_LIBRARY_PATH")"

  if grep -Fq GOODIX53X5_DUMP_DIR < <(strings "$library"); then
    [[ "$expected" == 1 ]] || milan_die "release payload contains debug diagnostics"
  else
    [[ "$expected" == 0 ]] || milan_die "debug payload lacks debug diagnostics"
  fi
}

milan_verify_manifest() {
  local root="$1" repo_dir="$2" manifest debug
  manifest="$(milan_root_path "$root" "$MILAN_BUILD_ENV")"
  milan_files verify-build "$root"
  milan_load_layout "$root"
  [[ "$(milan_manifest_value "$manifest" LIBFPRINT_REVISION)" == "$MILAN_LIBFPRINT_REVISION" ]] || milan_die "libfprint revision mismatch"
  [[ "$(milan_manifest_value "$manifest" LIBFPRINT_SOURCE_TREE)" == "$MILAN_LIBFPRINT_SOURCE_TREE" ]] || milan_die "libfprint source tree mismatch"
  [[ "$(milan_manifest_value "$manifest" FPRINTD_REVISION)" == "$MILAN_FPRINTD_REVISION" ]] || milan_die "fprintd revision mismatch"
  [[ "$(milan_manifest_value "$manifest" FPRINTD_SOURCE_TREE)" == "$MILAN_FPRINTD_SOURCE_TREE" ]] || milan_die "fprintd source tree mismatch"
  [[ "$(milan_manifest_value "$manifest" LIBFPRINT_PATCH_SHA256)" == "$MILAN_LIBFPRINT_PATCH_SHA256" ]] || milan_die "libfprint patch manifest mismatch"
  [[ "$(milan_manifest_value "$manifest" LIBFPRINT_USB_PERSIST_PATCH_SHA256)" == "$MILAN_LIBFPRINT_USB_PERSIST_PATCH_SHA256" ]] || milan_die "libfprint USB persist patch manifest mismatch"
  [[ "$(milan_manifest_value "$manifest" LIBFPRINT_IDLE_SUSPEND_NOTIFY_PATCH_SHA256)" == "$MILAN_LIBFPRINT_IDLE_SUSPEND_NOTIFY_PATCH_SHA256" ]] || milan_die "libfprint idle suspend notification patch manifest mismatch"
  [[ "$(milan_manifest_value "$manifest" FPRINTD_PATCH_SHA256)" == "$MILAN_FPRINTD_PATCH_SHA256" ]] || milan_die "fprintd patch manifest mismatch"
  [[ "$(milan_manifest_value "$manifest" OVERLAY_INPUT_SHA256)" == "$(milan_overlay_input_sha256 "$repo_dir")" ]] || milan_die "overlay input mismatch"
  debug="$(milan_manifest_value "$manifest" GOODIX53X5_DEBUG)"
  [[ "$debug" == 0 || "$debug" == 1 ]] || milan_die "invalid debug manifest value"
  milan_verify_repo_inputs "$repo_dir"
  [[ "$(readlink "$(milan_root_path "$root" "$MILAN_LIBDIR/libfprint-2.so.2")")" == libfprint-2.so.2.0.0 ]] || milan_die "invalid soname symlink"
  milan_verify_debug_census "$root" "$debug"
  local path
  for path in "$MILAN_DAEMON_PATH" "$MILAN_LIBDIR/security/pam_fprintd.so" \
      /usr/bin/fprintd-enroll /usr/bin/fprintd-verify /usr/bin/fprintd-list /usr/bin/fprintd-delete \
      /usr/lib/systemd/system/fprintd.service \
      /usr/share/dbus-1/system-services/net.reactivated.Fprint.service \
      /usr/share/dbus-1/system.d/net.reactivated.Fprint.conf \
      /usr/share/polkit-1/actions/net.reactivated.fprint.device.policy \
      "$MILAN_UDEV_DIR/$MILAN_UDEV_RULE_NAME"; do
    [[ -f "$(milan_root_path "$root" "$path")" ]] || milan_die "incomplete runtime payload: $path"
  done
  for path in "$MILAN_DAEMON_PATH" /usr/bin/fprintd-enroll /usr/bin/fprintd-verify \
      /usr/bin/fprintd-list /usr/bin/fprintd-delete; do
    [[ -x "$(milan_root_path "$root" "$path")" ]] || milan_die "runtime file is not executable: $path"
  done
  path="$(milan_root_path "$root" /usr/lib/systemd/system/fprintd.service)"
  grep -Fxq "ExecStart=$MILAN_DAEMON_PATH" "$path" || milan_die "staged service selects wrong daemon"
  grep -Fxq 'StateDirectory=fprint' "$path" || milan_die "staged service has wrong print state directory"
}

milan_files() {
  python3 "$MILAN_FILES_HELPER" "$@"
}

milan_load_layout() {
  local manifest
  manifest="$(milan_root_path "$1" "$MILAN_BUILD_ENV")"
  [[ "$(milan_manifest_value "$manifest" FORMAT)" == 2 ]] || milan_die "unsupported manifest format"
  MILAN_LIBDIR="$(milan_manifest_value "$manifest" LIBDIR)"
  MILAN_LIBRARY_PATH="$(milan_manifest_value "$manifest" LIBRARY_PATH)"
  MILAN_DAEMON_PATH="$(milan_manifest_value "$manifest" DAEMON_PATH)"
  [[ "$MILAN_LIBDIR" =~ ^/usr/lib(64|/[a-zA-Z0-9_-]+)?$ &&
     "$MILAN_LIBRARY_PATH" == "$MILAN_LIBDIR/libfprint-2.so.2.0.0" &&
     ( "$MILAN_DAEMON_PATH" == /usr/lib/fprintd || "$MILAN_DAEMON_PATH" == /usr/libexec/fprintd ) ]] ||
    milan_die "invalid system layout in $manifest"
}

milan_refresh_linker() {
  ldconfig
}

milan_restore_labels() {
  milan_files relabel "$MILAN_INSTALL_ROOT"
}

milan_runtime_marker() {
  milan_root_path "$MILAN_INSTALL_ROOT" /run/goodix53x5-milan-maintenance
}

milan_check_admin_mask() {
  local state
  state="$(milan_systemctl is-enabled fprintd.service 2>/dev/null || true)"
  [[ "$state" != masked ]] || milan_die "fprintd has a permanent administrator mask; resolve it before installing"
}

milan_mask_runtime() {
  local marker
  marker="$(milan_runtime_marker)"
  [[ ! -L "$marker" && ( ! -e "$marker" || -f "$marker" ) ]] ||
    milan_die "unsafe maintenance marker: $marker"
  [[ ! -e "$marker" || "$(<"$marker")" == goodix53x5-milan-stack ]] ||
    milan_die "unmanaged maintenance marker: $marker"
  milan_systemctl mask --runtime --now fprintd.service || return
  printf 'goodix53x5-milan-stack\n' > "$marker" || return
  chmod 0600 "$marker" || return
  [[ "$(milan_systemctl show fprintd.service --property=LoadState --value)" == masked ]] || {
    printf 'error: fprintd is still activatable; check for a higher-priority unit in /etc/systemd/system\n' >&2
    return 1
  }
}

milan_unmask_runtime() {
  local marker
  marker="$(milan_runtime_marker)"
  [[ -f "$marker" && ! -L "$marker" && "$(<"$marker")" == goodix53x5-milan-stack ]] ||
    milan_die "refusing to unmask without an owned maintenance marker"
  # Never remove /etc's permanent administrator mask.
  milan_systemctl unmask --runtime fprintd.service || return
  rm -f -- "$marker"
}

milan_capture_directory() {
  local environment="$1" assignment dump_dir=
  for assignment in $environment; do
    case "$assignment" in
      GOODIX53X5_DUMP_DIR=*)
        [[ -z "$dump_dir" ]] || milan_die "duplicate capture directory in service Environment"
        dump_dir="${assignment#GOODIX53X5_DUMP_DIR=}" ;;
    esac
  done
  printf '%s\n' "$dump_dir"
}

milan_lock_install() {
  local lock
  lock="$(milan_root_path "$MILAN_INSTALL_ROOT" /run/goodix53x5-milan-install.lock)"
  mkdir -p "$(dirname "$lock")"
  exec 9>"$lock"
  flock -n 9 || milan_die "another Milan install/remove is active"
}

milan_require_root() {
  [[ "${EUID:-$(id -u)}" == 0 ]] || milan_die "run as root: sudo $0"
}

milan_systemctl() {
  timeout --foreground 300 systemctl "$@"
}

milan_apply_usb_persist() {
  local value="$1" root=/sys/bus/usb/devices
  local device product vendor

  [[ "$value" == 0 || "$value" == 1 ]] || milan_die "invalid USB persist value: $value"
  [[ -d "$root" ]] || return 0
  for device in "$root"/*; do
    [[ -f "$device/idVendor" && -f "$device/idProduct" ]] || continue
    read -r vendor < "$device/idVendor"
    read -r product < "$device/idProduct"
    [[ "${vendor,,}" == 27c6 ]] || continue
    case "${product,,}" in
      5335|5385|5395)
        if [[ ! -e "$device/power/persist" ]]; then
          milan_die "USB persistence is unavailable for $vendor:$product at $device"
        fi
        printf '%s\n' "$value" > "$device/power/persist" || return 1
        ;;
    esac
  done
}

milan_verify_usb_persist() {
  local root=/sys/bus/usb/devices
  local device product value vendor

  [[ -d "$root" ]] || return 0
  for device in "$root"/*; do
    [[ -f "$device/idVendor" && -f "$device/idProduct" ]] || continue
    read -r vendor < "$device/idVendor"
    read -r product < "$device/idProduct"
    [[ "${vendor,,}" == 27c6 ]] || continue
    case "${product,,}" in
      5335|5385|5395)
        [[ -r "$device/power/persist" ]] ||
          milan_die "USB persistence is unavailable for $vendor:$product at $device"
        read -r value < "$device/power/persist"
        [[ "$value" == 1 ]] || milan_die "USB persistence is disabled for $vendor:$product at $device"
        ;;
    esac
  done
}

milan_verify_state_directory() {
  local merged shown

  merged="$(milan_systemctl cat fprintd.service)" || milan_die "cannot read merged fprintd unit"
  [[ "$merged" == *"StateDirectory=fprint"* ]] || milan_die "StateDirectory=fprint is not preserved"
  shown="$(milan_systemctl show fprintd.service --property=StateDirectory --no-pager)" || milan_die "cannot query StateDirectory"
  [[ "$shown" == *"StateDirectory=fprint"* ]] || milan_die "merged StateDirectory is not fprint"
}

milan_verify_active_system() {
  local shown

  milan_verify_state_directory
  shown="$(milan_systemctl show fprintd.service --property=ExecStart --property=Environment --no-pager)" || milan_die "cannot query active service"
  [[ "$shown" == *"path=$MILAN_DAEMON_PATH ;"* ]] || milan_die "installed daemon is not selected"
  [[ "$shown" != *"LD_LIBRARY_PATH="* && "$shown" != *"LD_PRELOAD="* ]] || milan_die "service overrides the system library loader"
  milan_verify_ldd
}

milan_safe_remove_tree() {
  local path="$1" parent="$2" resolved_path resolved_parent

  resolved_path="$(readlink -m "$path")"
  resolved_parent="$(readlink -m "$parent")"
  [[ "$resolved_path" != "$resolved_parent" && "$(dirname "$resolved_path")" == "$resolved_parent" ]] ||
    milan_die "refusing unsafe tree removal: $path"
  rm -rf -- "$resolved_path"
}
