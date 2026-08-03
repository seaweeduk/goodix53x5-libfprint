#!/usr/bin/env bash

MILAN_LIBFPRINT_REVISION="0c97a47d8ef405cd577b87058c1e89cae9d242e7"
MILAN_FPRINTD_REVISION="b54a007ccf58ac0ae074c7151b223f35cbd17306"
MILAN_LIBFPRINT_SOURCE_TREE="2d08bc33d953cd17b315c5f5199aa7a0d0504506"
MILAN_FPRINTD_SOURCE_TREE="ff82f8c3c2ab936ddafec9e88e650c04cd6f4f1d"
MILAN_LIBFPRINT_PATCH_SHA256="fa9a4a89df02894a01013dc787d06cdbb74a4908b8e3cdc5da745e0265fb2f72"
MILAN_FPRINTD_PATCH_SHA256="5d87cd806587fa5f035847a38ba3155b38f9a612a3070d9abfa6f83114e58db8"
MILAN_PREFIX="/opt/goodix53x5-milan"
MILAN_SYSTEMD_DIR="/etc/systemd/system/fprintd.service.d"
MILAN_DROPIN_NAME="98-goodix53x5-milan-stack.conf"
MILAN_PAYLOAD_MARKER=".goodix53x5-milan-payload"
MILAN_OWNED_MARKER=".goodix53x5-milan-owned"

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
MILAN_INSTALL_ROOT="${GOODIX_MILAN_INSTALL_ROOT:-/}"
MILAN_SYSTEMCTL="${GOODIX_MILAN_SYSTEMCTL:-systemctl}"
MILAN_LDD="${GOODIX_MILAN_LDD:-ldd}"

milan_validate_test_overrides() {
  local name

  for name in GOODIX_MILAN_INSTALL_ROOT GOODIX_MILAN_SYSTEMD_DIR \
              GOODIX_MILAN_SYSTEMCTL GOODIX_MILAN_LDD GOODIX_MILAN_TEST_EUID \
              GOODIX_MILAN_TEST_FPRINTD_REVISION; do
    if [[ -n "${!name:-}" && "${GOODIX_MILAN_SELF_TEST:-0}" != 1 ]]; then
      milan_die "$name is accepted only by the repository self-test"
    fi
  done
  if [[ "${GOODIX_MILAN_SELF_TEST:-0}" == 1 ]]; then
    MILAN_PREFIX="${GOODIX_MILAN_TEST_PREFIX:-$MILAN_PREFIX}"
    MILAN_SYSTEMD_DIR="${GOODIX_MILAN_SYSTEMD_DIR:-$MILAN_SYSTEMD_DIR}"
  fi
}
milan_validate_test_overrides

milan_validate_prefix() {
  milan_require_absolute "Milan prefix" "$MILAN_PREFIX"
  if [[ "${GOODIX_MILAN_SELF_TEST:-0}" != 1 && "$MILAN_PREFIX" != /opt/goodix53x5-milan ]]; then
    milan_die "production shadow prefix must be /opt/goodix53x5-milan"
  fi
  case "$MILAN_PREFIX/" in
    /usr/*|/bin/*|/sbin/*|/lib/*|/lib64/*|/etc/*|/var/*)
      milan_die "refusing package/state path as shadow prefix: $MILAN_PREFIX"
      ;;
  esac
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

milan_actual_prefix() {
  milan_root_path "$MILAN_INSTALL_ROOT" "$MILAN_PREFIX"
}

milan_actual_systemd_dir() {
  milan_root_path "$MILAN_INSTALL_ROOT" "$MILAN_SYSTEMD_DIR"
}

milan_sha256() {
  sha256sum "$1" | cut -d ' ' -f 1
}

milan_overlay_input_sha256() {
  local repo_dir="$1"

  (
    cd "$repo_dir"
    find drivers/goodix53x5 tests -type f -print0 |
      LC_ALL=C sort -z |
      xargs -0 sha256sum
    sha256sum meson-integration.patch scripts/build-local.sh \
      patches/libfprint/libfprint-update-result.patch \
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
  local daemon_patch="$repo_dir/patches/fprintd/1.94.5-milan-update-save.patch"

  [[ "$(milan_sha256 "$lib_patch")" == "$MILAN_LIBFPRINT_PATCH_SHA256" ]] ||
    milan_die "libfprint patch digest mismatch"
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

milan_render_dropin() {
  local repo_dir="$1" line

  while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line//@GOODIX_MILAN_PREFIX@/$MILAN_PREFIX}"
    printf '%s\n' "$line"
  done < "$repo_dir/scripts/98-goodix53x5-milan-stack.conf.in"
}

milan_assert_safe_symlinks() {
  local prefix="$1" link target

  while IFS= read -r -d '' link; do
    target="$(readlink "$link")"
    case "$target" in
      /*|../*|*/../*|*/..) milan_die "payload symlink escapes prefix: $link -> $target" ;;
    esac
    case "$link" in
      "$prefix/lib/libfprint-2.so.2"|"$prefix/lib/libfprint-2.so") ;;
      *) milan_die "unexpected payload symlink: $link" ;;
    esac
  done < <(find "$prefix" -type l -print0)
}

milan_verify_ldd() {
  local prefix="$1" daemon="$1/fprintd/fprintd" library_dir="$1/lib" output resolved

  [[ -x "$daemon" ]] || milan_die "missing shadow daemon: $daemon"
  [[ -e "$library_dir/libfprint-2.so.2" ]] || milan_die "missing shadow libfprint soname"
  output="$(timeout --foreground 300 env LD_LIBRARY_PATH="$library_dir" "$MILAN_LDD" "$daemon")" ||
    milan_die "ldd failed for shadow daemon"
  resolved="$(grep -E '^[[:space:]]*libfprint-2\.so\.2[[:space:]]+=>' <<<"$output" | head -n 1 || true)"
  [[ "$resolved" == *"$library_dir/"* ]] || milan_die "shadow daemon resolves wrong libfprint: $resolved"
}

milan_verify_debug_census() {
  local prefix="$1" expected="$2" library="$1/lib/libfprint-2.so.2.0.0"

  if grep -Fq GOODIX53X5_DUMP_DIR < <(strings "$library"); then
    [[ "$expected" == 1 ]] || milan_die "release payload contains debug diagnostics"
  else
    [[ "$expected" == 0 ]] || milan_die "debug payload lacks debug diagnostics"
  fi
}

milan_verify_manifest() {
  local prefix="$1" repo_dir="$2" manifest="$1/manifest/build.env" sums="$1/manifest/SHA256SUMS" debug file

  [[ -f "$prefix/$MILAN_PAYLOAD_MARKER" && -f "$manifest" && -f "$sums" ]] ||
    milan_die "payload markers or manifests are missing"
  [[ "$(milan_manifest_value "$manifest" FORMAT)" == 1 ]] || milan_die "unsupported manifest format"
  [[ "$(milan_manifest_value "$manifest" PREFIX)" == "$MILAN_PREFIX" ]] || milan_die "payload prefix mismatch"
  [[ "$(milan_manifest_value "$manifest" LIBFPRINT_REVISION)" == "$MILAN_LIBFPRINT_REVISION" ]] || milan_die "libfprint revision mismatch"
  [[ "$(milan_manifest_value "$manifest" LIBFPRINT_SOURCE_TREE)" == "$MILAN_LIBFPRINT_SOURCE_TREE" ]] || milan_die "libfprint source tree mismatch"
  [[ "$(milan_manifest_value "$manifest" FPRINTD_REVISION)" == "$MILAN_FPRINTD_REVISION" ]] || milan_die "fprintd revision mismatch"
  [[ "$(milan_manifest_value "$manifest" FPRINTD_SOURCE_TREE)" == "$MILAN_FPRINTD_SOURCE_TREE" ]] || milan_die "fprintd source tree mismatch"
  [[ "$(milan_manifest_value "$manifest" LIBFPRINT_PATCH_SHA256)" == "$MILAN_LIBFPRINT_PATCH_SHA256" ]] || milan_die "libfprint patch manifest mismatch"
  [[ "$(milan_manifest_value "$manifest" FPRINTD_PATCH_SHA256)" == "$MILAN_FPRINTD_PATCH_SHA256" ]] || milan_die "fprintd patch manifest mismatch"
  [[ "$(milan_manifest_value "$manifest" OVERLAY_INPUT_SHA256)" == "$(milan_overlay_input_sha256 "$repo_dir")" ]] || milan_die "overlay input mismatch"
  debug="$(milan_manifest_value "$manifest" GOODIX53X5_DEBUG)"
  [[ "$debug" == 0 || "$debug" == 1 ]] || milan_die "invalid debug manifest value"
  milan_verify_repo_inputs "$repo_dir"
  (cd "$prefix" && sha256sum --check manifest/SHA256SUMS >/dev/null) || milan_die "payload digest verification failed"
  while IFS= read -r -d '' file; do
    file="${file#"$prefix/"}"
    grep -Fxq -- "./$file" < <(cut -c 67- "$sums") || milan_die "unmanifested payload file: $file"
  done < <(find "$prefix" -type f ! -path "$sums" ! -name "$MILAN_OWNED_MARKER" -print0)
  [[ "$(readlink "$prefix/lib/libfprint-2.so.2")" == libfprint-2.so.2.0.0 ]] || milan_die "invalid soname symlink"
  [[ "$(readlink "$prefix/lib/libfprint-2.so")" == libfprint-2.so.2 ]] || milan_die "invalid linker symlink"
  milan_assert_safe_symlinks "$prefix"
  milan_verify_ldd "$prefix"
  milan_verify_debug_census "$prefix" "$debug"
}

milan_verify_owned_marker() {
  local prefix="$1" marker="$1/$MILAN_OWNED_MARKER"

  [[ -f "$marker" ]] || milan_die "refusing unmanaged shadow prefix: $prefix"
  [[ "$(milan_manifest_value "$marker" FORMAT)" == 1 ]] || milan_die "invalid ownership marker"
  [[ "$(milan_manifest_value "$marker" PREFIX)" == "$MILAN_PREFIX" ]] || milan_die "ownership prefix mismatch"
  [[ "$(milan_manifest_value "$marker" MANAGED_BY)" == goodix53x5-milan-stack ]] || milan_die "ownership marker mismatch"
  [[ "$(milan_manifest_value "$marker" BUILD_MANIFEST_SHA256)" == "$(milan_sha256 "$prefix/manifest/build.env")" ]] || milan_die "ownership manifest mismatch"
}

milan_require_root() {
  local uid="${EUID:-$(id -u)}"

  if [[ -n "${GOODIX_MILAN_TEST_EUID:-}" ]]; then
    [[ "${GOODIX_MILAN_SELF_TEST:-0}" == 1 ]] || milan_die "test EUID override rejected"
    uid="$GOODIX_MILAN_TEST_EUID"
  fi
  [[ "$uid" == 0 ]] || milan_die "run as root: sudo $0"
}

milan_systemctl() {
  timeout --foreground 300 "$MILAN_SYSTEMCTL" "$@"
}

milan_verify_state_directory() {
  local merged shown

  merged="$(milan_systemctl cat fprintd.service)" || milan_die "cannot read merged fprintd unit"
  [[ "$merged" == *"StateDirectory=fprint"* ]] || milan_die "StateDirectory=fprint is not preserved"
  shown="$(milan_systemctl show fprintd.service --property=StateDirectory --no-pager)" || milan_die "cannot query StateDirectory"
  [[ "$shown" == *"StateDirectory=fprint"* ]] || milan_die "merged StateDirectory is not fprint"
}

milan_verify_active_shadow() {
  local shown

  milan_verify_state_directory
  shown="$(milan_systemctl show fprintd.service --property=ExecStart --property=Environment --no-pager)" || milan_die "cannot query active service"
  [[ "$shown" == *"$MILAN_PREFIX/fprintd/fprintd"* ]] || milan_die "shadow daemon is not selected"
  [[ "$shown" == *"LD_LIBRARY_PATH=$MILAN_PREFIX/lib"* ]] || milan_die "shadow library path is not selected"
}

milan_verify_packaged_selected() {
  local shown

  shown="$(milan_systemctl show fprintd.service --property=ExecStart --no-pager)" || milan_die "cannot query packaged service"
  [[ "$shown" != *"$MILAN_PREFIX/"* ]] || milan_die "shadow daemon remains selected"
}

milan_safe_remove_tree() {
  local path="$1" parent="$2" resolved_path resolved_parent

  resolved_path="$(readlink -m "$path")"
  resolved_parent="$(readlink -m "$parent")"
  [[ "$resolved_path" != "$resolved_parent" && "$(dirname "$resolved_path")" == "$resolved_parent" ]] ||
    milan_die "refusing unsafe tree removal: $path"
  rm -rf -- "$resolved_path"
}
