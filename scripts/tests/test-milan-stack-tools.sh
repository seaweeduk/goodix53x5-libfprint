#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/../.." && pwd)"
state_home="${XDG_STATE_HOME:-${HOME:?HOME is required}/.local/state}"
test_root="${GOODIX_MILAN_SELF_TEST_ROOT:-$state_home/goodix53x5-milan-stack-self-test}"

case "$test_root" in
  */goodix53x5-milan-stack-self-test) ;;
  *) printf 'refusing unsafe self-test root: %s\n' "$test_root" >&2; exit 1 ;;
esac
case "$test_root/" in
  /tmp/*|/var/tmp/*|/run/*) printf 'self-test root must be persistent\n' >&2; exit 1 ;;
esac

rm -rf -- "$test_root"
mkdir -p "$test_root/bin" "$test_root/state" "$test_root/fake-root"
trap 'rm -rf -- "$test_root"' EXIT

pass_count=0
fail() { printf 'not ok: %s\n' "$*" >&2; exit 1; }
pass() { pass_count=$((pass_count + 1)); printf 'ok %d - %s\n' "$pass_count" "$1"; }
expect_failure() {
  local label="$1"
  shift
  if "$@" >"$test_root/state/last.stdout" 2>"$test_root/state/last.stderr"; then
    fail "$label unexpectedly succeeded"
  fi
  pass "$label"
}

cat > "$test_root/bin/ldd" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ -e "$FAKE_STATE/ldd-wrong" ]]; then
  printf '\tlibfprint-2.so.2 => /usr/lib/libfprint-2.so.2 (0x0)\n'
else
  printf '\tlibfprint-2.so.2 => %s/libfprint-2.so.2 (0x0)\n' "$LD_LIBRARY_PATH"
fi
EOF

cat > "$test_root/bin/systemctl" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
command_name="${1:-}"
dropin="$FAKE_ROOT/etc/systemd/system/fprintd.service.d/98-goodix53x5-milan-stack.conf"
printf '%s\n' "$*" >> "$FAKE_STATE/systemctl.log"
case "$command_name" in
  cat)
    printf '[Service]\nExecStart=/usr/lib/fprintd\nStateDirectory=fprint\n'
    [[ ! -f "$dropin" ]] || command cat "$dropin"
    ;;
  show)
    printf 'StateDirectory=fprint\n'
    if [[ -f "$dropin" ]]; then
      printf 'ExecStart={ path=/opt/goodix53x5-milan/fprintd/fprintd ; }\n'
      printf 'Environment=LD_LIBRARY_PATH=/opt/goodix53x5-milan/lib\n'
    else
      printf 'ExecStart={ path=/usr/lib/fprintd ; }\nEnvironment=\n'
    fi
    ;;
  daemon-reload)
    if [[ -e "$FAKE_STATE/fail-daemon-reload-once" ]]; then
      rm -f -- "$FAKE_STATE/fail-daemon-reload-once"
      exit 1
    fi
    ;;
  restart)
    if [[ -f "$dropin" && -e "$FAKE_STATE/fail-shadow-restart-once" ]]; then
      rm -f -- "$FAKE_STATE/fail-shadow-restart-once"
      exit 1
    fi
    if [[ ! -f "$dropin" && -e "$FAKE_STATE/fail-packaged-restart-once" ]]; then
      rm -f -- "$FAKE_STATE/fail-packaged-restart-once"
      exit 1
    fi
    ;;
  stop) ;;
  *) exit 1 ;;
esac
EOF
cat > "$test_root/bin/udevadm" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$FAKE_STATE/udevadm.log"
[[ "${1:-}" == control && "${2:-}" == --reload-rules ]]
EOF
chmod 0755 "$test_root/bin/ldd" "$test_root/bin/systemctl" \
  "$test_root/bin/udevadm"

stack_root="$test_root/stack"
fake_root="$test_root/fake-root"
fake_prefix="$fake_root/opt/goodix53x5-milan"
fake_systemd="$fake_root/etc/systemd/system/fprintd.service.d"
fake_udev="$fake_root/etc/udev/rules.d"
fake_sysfs="$test_root/sysfs-usb-devices"
fake_usb_device="$fake_sysfs/3-9"
payload_prefix="$stack_root/builds/build-fixture/payload/opt/goodix53x5-milan"
payload_udev="$payload_prefix/share/udev/rules.d"
mkdir -p "$payload_prefix/fprintd" "$payload_prefix/lib" \
  "$payload_prefix/manifest" "$payload_udev"
mkdir -p "$fake_systemd" "$fake_udev" "$fake_root/var/lib/fprint" \
  "$fake_root/opt" "$fake_usb_device/power"
printf '#!/usr/bin/env bash\nexit 0\n' > "$payload_prefix/fprintd/fprintd"
chmod 0755 "$payload_prefix/fprintd/fprintd"
printf 'release fixture library\n' > "$payload_prefix/lib/libfprint-2.so.2.0.0"
ln -s libfprint-2.so.2.0.0 "$payload_prefix/lib/libfprint-2.so.2"
ln -s libfprint-2.so.2 "$payload_prefix/lib/libfprint-2.so"
cp "$repo_dir/udev/99-goodix53x5-milan-persist.rules" "$payload_udev/"
printf '27c6\n' > "$fake_usb_device/idVendor"
printf '5335\n' > "$fake_usb_device/idProduct"
printf '0\n' > "$fake_usb_device/power/persist"
printf 'goodix53x5-milan-stack-payload-v1\n' > "$payload_prefix/.goodix53x5-milan-payload"
printf 'fingerprint state sentinel\n' > "$fake_root/var/lib/fprint/keep.fp3"

overlay_input_sha256="$(
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
)"
cat > "$payload_prefix/manifest/build.env" <<EOF
FORMAT=1
PREFIX=/opt/goodix53x5-milan
GOODIX53X5_DEBUG=0
LIBFPRINT_REVISION=0c97a47d8ef405cd577b87058c1e89cae9d242e7
LIBFPRINT_SOURCE_TREE=2d08bc33d953cd17b315c5f5199aa7a0d0504506
FPRINTD_REVISION=b54a007ccf58ac0ae074c7151b223f35cbd17306
FPRINTD_SOURCE_TREE=ff82f8c3c2ab936ddafec9e88e650c04cd6f4f1d
LIBFPRINT_PATCH_SHA256=fa9a4a89df02894a01013dc787d06cdbb74a4908b8e3cdc5da745e0265fb2f72
LIBFPRINT_USB_PERSIST_PATCH_SHA256=743c13782228869b8b5ea834caa096abadd38e5542303d7af8bc7acb4c925ae0
LIBFPRINT_IDLE_SUSPEND_NOTIFY_PATCH_SHA256=ec357fef155b2b6a0be6d91e697e4c4cbd3f5f4cec5218cd95b94008d7e7e548
FPRINTD_PATCH_SHA256=5d87cd806587fa5f035847a38ba3155b38f9a612a3070d9abfa6f83114e58db8
OVERLAY_REVISION=fixture
OVERLAY_INPUT_SHA256=$overlay_input_sha256
MESON_INTEGRATION_SHA256=fixture
BUILD_LOCAL_SHA256=fixture
STACK_BUILD_SHA256=fixture
STACK_COMMON_SHA256=fixture
BUILT_UTC=2026-08-01T00:00:00Z
EOF
(
  cd "$payload_prefix"
  find . -type f ! -path ./manifest/SHA256SUMS -print0 |
    LC_ALL=C sort -z |
    xargs -0 sha256sum > manifest/SHA256SUMS
)
ln -s build-fixture "$stack_root/builds/current"

tool_env=(
  env
  PATH="$test_root/bin:$PATH"
  GOODIX_MILAN_SELF_TEST=1
  GOODIX_MILAN_TEST_EUID=0
  GOODIX_MILAN_STACK_ROOT="$stack_root"
  GOODIX_MILAN_INSTALL_ROOT="$fake_root"
  GOODIX_MILAN_SYSFS_USB_DEVICES="$fake_sysfs"
  GOODIX_MILAN_SYSTEMCTL="$test_root/bin/systemctl"
  GOODIX_MILAN_LDD="$test_root/bin/ldd"
  FAKE_ROOT="$fake_root"
  FAKE_STATE="$test_root/state"
)

"${tool_env[@]}" "$repo_dir/scripts/status-milan-stack-local.sh" --build >/dev/null
pass "verified checksummed release payload"

mkdir "$fake_prefix"
expect_failure "unmanaged prefix rejection" "${tool_env[@]}" "$repo_dir/scripts/install-milan-stack-local.sh"
rmdir "$fake_prefix"

printf '[Service]\nExecStart=/unmanaged\n' > "$fake_systemd/98-goodix53x5-milan-stack.conf"
expect_failure "unmanaged drop-in rejection" "${tool_env[@]}" "$repo_dir/scripts/install-milan-stack-local.sh"
rm -f -- "$fake_systemd/98-goodix53x5-milan-stack.conf"

touch "$test_root/state/ldd-wrong"
stops_before="$(grep -c '^stop ' "$test_root/state/systemctl.log" 2>/dev/null || true)"
expect_failure "wrong linked libfprint rejected before stop" "${tool_env[@]}" "$repo_dir/scripts/install-milan-stack-local.sh"
stops_after="$(grep -c '^stop ' "$test_root/state/systemctl.log" 2>/dev/null || true)"
[[ "$stops_before" == "$stops_after" ]] || fail "preflight failure stopped service"
rm -f -- "$test_root/state/ldd-wrong"

touch "$test_root/state/fail-daemon-reload-once"
expect_failure "interrupted install rollback" "${tool_env[@]}" "$repo_dir/scripts/install-milan-stack-local.sh"
[[ ! -e "$fake_prefix" && ! -e "$fake_systemd/98-goodix53x5-milan-stack.conf" &&
   ! -e "$fake_udev/99-goodix53x5-milan-persist.rules" ]] ||
  fail "interrupted install left managed files"
[[ "$(<"$fake_usb_device/power/persist")" == 0 ]] ||
  fail "interrupted install left USB persistence enabled"

touch "$test_root/state/fail-shadow-restart-once"
expect_failure "failed shadow restart rollback" "${tool_env[@]}" "$repo_dir/scripts/install-milan-stack-local.sh"
[[ ! -e "$fake_prefix" && ! -e "$fake_systemd/98-goodix53x5-milan-stack.conf" &&
   ! -e "$fake_udev/99-goodix53x5-milan-persist.rules" ]] ||
  fail "restart failure left managed files"
[[ "$(<"$fake_usb_device/power/persist")" == 0 ]] ||
  fail "restart failure left USB persistence enabled"

"${tool_env[@]}" "$repo_dir/scripts/install-milan-stack-local.sh" >/dev/null
"${tool_env[@]}" "$repo_dir/scripts/status-milan-stack-local.sh" --installed >/dev/null
[[ -f "$fake_root/var/lib/fprint/keep.fp3" ]] || fail "install changed fingerprint state"
[[ -f "$fake_udev/99-goodix53x5-milan-persist.rules" ]] ||
  fail "install omitted USB persistence rule"
[[ "$(<"$fake_usb_device/power/persist")" == 1 ]] ||
  fail "install did not enable USB persistence"
pass "atomic install and installed status"

touch "$test_root/state/fail-packaged-restart-once"
expect_failure "remove rollback retains working shadow stack" "${tool_env[@]}" "$repo_dir/scripts/remove-milan-stack-local.sh"
[[ -f "$fake_prefix/.goodix53x5-milan-owned" ]] || fail "remove rollback lost prefix"
[[ -f "$fake_systemd/98-goodix53x5-milan-stack.conf" ]] || fail "remove rollback lost drop-in"
[[ -f "$fake_udev/99-goodix53x5-milan-persist.rules" ]] ||
  fail "remove rollback lost USB persistence rule"
[[ "$(<"$fake_usb_device/power/persist")" == 1 ]] ||
  fail "remove rollback left USB persistence disabled"
"${tool_env[@]}" "$repo_dir/scripts/status-milan-stack-local.sh" --installed >/dev/null

"${tool_env[@]}" "$repo_dir/scripts/remove-milan-stack-local.sh" >/dev/null
"${tool_env[@]}" "$repo_dir/scripts/status-milan-stack-local.sh" --packaged >/dev/null
[[ -f "$fake_root/var/lib/fprint/keep.fp3" ]] || fail "remove changed fingerprint state"
[[ ! -e "$fake_udev/99-goodix53x5-milan-persist.rules" ]] ||
  fail "remove left USB persistence rule installed"
[[ "$(<"$fake_usb_device/power/persist")" == 0 ]] ||
  fail "remove left USB persistence enabled"
pass "transactional remove and packaged status"

"${tool_env[@]}" "$repo_dir/scripts/remove-milan-stack-local.sh" >/dev/null
pass "idempotent remove"
printf '1..%d\n' "$pass_count"
