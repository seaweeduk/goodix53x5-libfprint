#!/usr/bin/env bash
# Build the paired libfprint/fprintd shadow stack around build-local.sh.

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
# shellcheck source=scripts/lib/milan-stack-common.sh
source "$script_dir/lib/milan-stack-common.sh"

milan_validate_prefix
milan_reject_ephemeral_root "GOODIX_MILAN_STACK_ROOT" "$MILAN_STACK_ROOT"
for command in git flock timeout meson ninja sha256sum ldd install strings od tr; do
  milan_require_command "$command"
done
milan_verify_repo_inputs "$repo_dir"

case "${GOODIX53X5_DEBUG:-0}" in
  0) debug_enabled=false; debug_manifest=0; build_kind=release ;;
  1) debug_enabled=true; debug_manifest=1; build_kind=debug ;;
  *) milan_die "GOODIX53X5_DEBUG must be 0 or 1" ;;
esac
debug_build_id=
debug_source_id=
if [[ "$debug_manifest" == 1 ]]; then
  debug_build_id="$(od -An -N32 -tx1 /dev/urandom | tr -d '[:space:]')"
  debug_source_id="$($repo_dir/tools/milan-parity/build-identity "$repo_dir")"
  if [[ ! $debug_build_id =~ ^[0-9a-f]{64}$ ||
        ! $debug_source_id =~ ^[0-9a-f]{64}$ ]]; then
    milan_die "failed to produce valid Goodix debug build provenance"
  fi
fi

source_root="$MILAN_STACK_ROOT/sources"
mkdir -p "$source_root"
libfprint_sibling="$(cd "$repo_dir/.." && pwd)/goodix-fp-dump/derived/external-references/libfprint-v1.94.10"
if [[ -n "${GOODIX_MILAN_LIBFPRINT_SOURCE:-}" ]]; then
  libfprint_pristine="$GOODIX_MILAN_LIBFPRINT_SOURCE"
elif [[ -d "$libfprint_sibling/.git" ]]; then
  libfprint_pristine="$libfprint_sibling"
else
  libfprint_pristine="$source_root/libfprint-v1.94.10"
fi
if [[ ! -e "$libfprint_pristine" ]]; then
  milan_run_stage "clone libfprint source" git clone --no-checkout \
    "${GOODIX_MILAN_LIBFPRINT_URL:-https://gitlab.freedesktop.org/libfprint/libfprint.git}" \
    "$libfprint_pristine"
  milan_run_stage "checkout exact libfprint revision" git -C "$libfprint_pristine" \
    checkout --detach "$MILAN_LIBFPRINT_REVISION"
fi
milan_verify_git_pristine "$libfprint_pristine" "$MILAN_LIBFPRINT_REVISION" "libfprint"

"$script_dir/fetch-fprintd-local.sh"
fprintd_sibling="$(cd "$repo_dir/.." && pwd)/goodix-fp-dump/derived/milan-stack/sources/fprintd-v1.94.5"
if [[ -n "${GOODIX_MILAN_FPRINTD_SOURCE:-}" ]]; then
  fprintd_pristine="$GOODIX_MILAN_FPRINTD_SOURCE"
elif [[ -d "$fprintd_sibling/.git" ]]; then
  fprintd_pristine="$fprintd_sibling"
else
  fprintd_pristine="$source_root/fprintd-v1.94.5"
fi
milan_verify_git_pristine "$fprintd_pristine" "$MILAN_FPRINTD_REVISION" "fprintd"
"$repo_dir/patches/libfprint/verify-update-result-patch.sh" "$libfprint_pristine"
"$repo_dir/patches/libfprint/verify-goodix53x5-usb-persist-patch.sh" "$libfprint_pristine"
"$repo_dir/patches/fprintd/verify-update-save-patch.sh" "$fprintd_pristine"

mkdir -p "$MILAN_STACK_ROOT/builds"
exec 9>"$MILAN_STACK_ROOT/.build.lock"
flock -n 9 || milan_die "another Milan stack build is active"
staging="$MILAN_STACK_ROOT/builds/.staging.$$"
mkdir "$staging"
cleanup() {
  [[ ! -e "$staging" ]] || milan_safe_remove_tree "$staging" "$MILAN_STACK_ROOT/builds"
}
trap cleanup EXIT

mkdir "$staging/libfprint-overlay"
milan_run_stage "seed pinned libfprint checkout" git clone --local --no-hardlinks \
  "$libfprint_pristine" "$staging/libfprint-overlay/libfprint"
milan_run_stage "apply repository libfprint and driver overlay" env \
  GOODIX_LOCAL_BUILD_DIR="$staging/libfprint-overlay" \
  GOODIX_LIBFPRINT_REF="$MILAN_LIBFPRINT_REVISION" \
  GOODIX_LIBFPRINT_OFFLINE=1 \
  GOODIX_MESON_BUILDDIR=builddir \
  GOODIX53X5_DEBUG="$debug_manifest" \
  GOODIX53X5_INTERNAL_BUILD_ID="$debug_build_id" \
  GOODIX53X5_INTERNAL_SOURCE_ID="$debug_source_id" \
  "$script_dir/build-local.sh"

libfprint_source="$staging/libfprint-overlay/libfprint"
libfprint_build="$libfprint_source/builddir"
git -C "$libfprint_source" apply --reverse --check \
  "$repo_dir/patches/libfprint/libfprint-update-result.patch"
git -C "$libfprint_source" apply --reverse --check \
  "$repo_dir/patches/libfprint/libfprint-goodix53x5-usb-persist.patch"
milan_run_stage "configure paired shadow libfprint" meson setup "$libfprint_build" \
  "$libfprint_source" --reconfigure --prefix="$MILAN_PREFIX" --libdir=lib \
  -Ddrivers=goodix53x5 -Dudev_hwdb=disabled -Dudev_rules=disabled \
  -Dintrospection=false -Dinstalled-tests=false -Ddoc=false \
  -Dgoodix53x5_debug="$debug_enabled" \
  -Dgoodix53x5_debug_build_id="$debug_build_id" \
  -Dgoodix53x5_debug_source_id="$debug_source_id"
milan_run_stage "build paired shadow libfprint" ninja -C "$libfprint_build"
milan_run_stage "test libfprint update result" meson test -C "$libfprint_build" \
  --print-errorlogs fpi-device
milan_run_stage "test Milan synthetic public contract" meson test -C "$libfprint_build" \
  --print-errorlogs goodix53x5-milan-synthetic
milan_run_stage "test Milan state invariants" meson test -C "$libfprint_build" \
  --print-errorlogs goodix53x5-milan-state
milan_run_stage "test Milan runtime public contract" meson test -C "$libfprint_build" \
  --print-errorlogs goodix53x5-milan-runtime

fprintd_source="$staging/fprintd-source"
fprintd_build="$staging/fprintd-build"
milan_run_stage "clone pinned fprintd checkout" git clone --local --no-hardlinks \
  "$fprintd_pristine" "$fprintd_source"
git -C "$fprintd_source" apply "$repo_dir/patches/fprintd/1.94.5-milan-update-save.patch"
git -C "$fprintd_source" apply --reverse --check \
  "$repo_dir/patches/fprintd/1.94.5-milan-update-save.patch"
milan_run_stage "configure fprintd against paired libfprint" meson devenv -C "$libfprint_build" \
  meson setup "$fprintd_build" "$fprintd_source" --prefix="$MILAN_PREFIX" \
  --libexecdir=fprintd -Dpam=false -Dman=false -Dsystemd=false -Dgtk_doc=false
milan_run_stage "build paired fprintd daemon" meson devenv -C "$libfprint_build" \
  ninja -C "$fprintd_build" src/fprintd

payload="$staging/payload"
payload_prefix="$(milan_root_path "$payload" "$MILAN_PREFIX")"
payload_udev_dir="$payload_prefix/share/udev/rules.d"
mkdir -p "$payload_prefix/fprintd" "$payload_prefix/manifest" "$payload_udev_dir"
milan_run_stage "stage shadow libfprint" env DESTDIR="$payload" ninja -C "$libfprint_build" install
install -m 0755 "$fprintd_build/src/fprintd" "$payload_prefix/fprintd/fprintd"
install -m 0644 "$repo_dir/udev/$MILAN_UDEV_RULE_NAME" "$payload_udev_dir/$MILAN_UDEV_RULE_NAME"
printf '%s\n' goodix53x5-milan-stack-payload-v1 > "$payload_prefix/$MILAN_PAYLOAD_MARKER"

overlay_revision="$(git -C "$repo_dir" rev-parse HEAD)"
overlay_input_sha256="$(milan_overlay_input_sha256 "$repo_dir")"
cat > "$payload_prefix/manifest/build.env" <<EOF
FORMAT=1
PREFIX=$MILAN_PREFIX
GOODIX53X5_DEBUG=$debug_manifest
GOODIX53X5_DEBUG_BUILD_ID=$debug_build_id
GOODIX53X5_DEBUG_SOURCE_ID=$debug_source_id
LIBFPRINT_REVISION=$MILAN_LIBFPRINT_REVISION
LIBFPRINT_SOURCE_TREE=$(git -C "$libfprint_pristine" rev-parse HEAD^{tree})
FPRINTD_REVISION=$MILAN_FPRINTD_REVISION
FPRINTD_SOURCE_TREE=$(git -C "$fprintd_pristine" rev-parse HEAD^{tree})
LIBFPRINT_PATCH_SHA256=$MILAN_LIBFPRINT_PATCH_SHA256
LIBFPRINT_USB_PERSIST_PATCH_SHA256=$MILAN_LIBFPRINT_USB_PERSIST_PATCH_SHA256
FPRINTD_PATCH_SHA256=$MILAN_FPRINTD_PATCH_SHA256
OVERLAY_REVISION=$overlay_revision
OVERLAY_INPUT_SHA256=$overlay_input_sha256
MESON_INTEGRATION_SHA256=$(milan_sha256 "$repo_dir/meson-integration.patch")
BUILD_LOCAL_SHA256=$(milan_sha256 "$script_dir/build-local.sh")
STACK_BUILD_SHA256=$(milan_sha256 "$script_dir/build-milan-stack-local.sh")
STACK_COMMON_SHA256=$(milan_sha256 "$script_dir/lib/milan-stack-common.sh")
BUILT_UTC=$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF
(
  cd "$payload_prefix"
  find . -type f ! -path ./manifest/SHA256SUMS -print0 |
    LC_ALL=C sort -z |
    xargs -0 sha256sum > manifest/SHA256SUMS
)
milan_verify_manifest "$payload_prefix" "$repo_dir"

for intermediate in "$staging/libfprint-overlay" "$fprintd_source" "$fprintd_build"; do
  [[ -d "$intermediate" && ! -L "$intermediate" ]] ||
    milan_die "refusing unexpected build intermediate: $intermediate"
  milan_safe_remove_tree "$intermediate" "$staging"
done

build_id="$(date -u +%Y%m%dT%H%M%SZ)-$build_kind-${overlay_revision:0:12}"
published="$MILAN_STACK_ROOT/builds/$build_id"
[[ ! -e "$published" && ! -L "$published" ]] || published="$published-$$"
[[ ! -e "$published" && ! -L "$published" ]] || milan_die "build publication path already exists: $published"
mv "$staging" "$published"
trap - EXIT
link_tmp="$MILAN_STACK_ROOT/builds/.current.$$"
ln -s "$(basename "$published")" "$link_tmp"
mv -Tf "$link_tmp" "$MILAN_STACK_ROOT/builds/current"
shopt -s dotglob
for obsolete in "$MILAN_STACK_ROOT/builds"/*; do
  [[ "$obsolete" != "$published" && "$obsolete" != "$MILAN_STACK_ROOT/builds/current" ]] || continue
  if [[ -L "$obsolete" ]]; then
    rm -- "$obsolete"
    continue
  fi
  [[ -d "$obsolete" ]] || continue
  milan_safe_remove_tree "$obsolete" "$MILAN_STACK_ROOT/builds"
done
shopt -u dotglob
milan_note "published verified $build_kind Milan stack: $published"
milan_note "no running service was changed"
