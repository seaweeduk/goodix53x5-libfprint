#!/usr/bin/env bash
# Build libfprint with this driver from inside the driver repository.

set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="${GOODIX_LOCAL_BUILD_DIR:-$repo_dir/.build}"
libfprint_dir="$work_dir/libfprint"
libfprint_ref="${GOODIX_LIBFPRINT_REF:-v1.94.10}"
build_dir="${GOODIX_MESON_BUILDDIR:-builddir}"
debug_enabled=false
if [[ "${GOODIX53X5_DEBUG:-0}" == 1 ]]; then
  debug_enabled=true
fi
meson_options=(
  --prefix=/usr
  -Ddrivers=goodix53x5
  -Dudev_hwdb=disabled
  -Dudev_rules=disabled
  -Dintrospection=false
  -Dinstalled-tests=false
  -Ddoc=false
  -Dgoodix53x5_debug="$debug_enabled"
)

mkdir -p "$work_dir"

if [ ! -d "$libfprint_dir/.git" ] &&
   [[ "${GOODIX_LIBFPRINT_OFFLINE:-0}" == 1 ]]; then
  printf 'offline build requires an existing checkout at %s\n' \
    "$libfprint_dir" >&2
  exit 1
elif [ ! -d "$libfprint_dir/.git" ]; then
  git clone https://gitlab.freedesktop.org/libfprint/libfprint.git "$libfprint_dir"
fi

if [[ "${GOODIX_LIBFPRINT_OFFLINE:-0}" != 1 ]]; then
  git -C "$libfprint_dir" fetch --tags --quiet
fi
git -C "$libfprint_dir" reset --hard --quiet
git -C "$libfprint_dir" checkout --quiet "$libfprint_ref"
git -C "$libfprint_dir" reset --hard --quiet "$libfprint_ref"

# Patch A is revision-pinned and must be checked against a pristine checkout.
# The verifier rejects both the wrong commit and any context drift.
"$repo_dir/patches/libfprint/verify-update-result-patch.sh" "$libfprint_dir"
"$repo_dir/patches/libfprint/verify-goodix53x5-usb-persist-patch.sh" "$libfprint_dir"
git -C "$libfprint_dir" apply \
  "$repo_dir/patches/libfprint/libfprint-update-result.patch"
git -C "$libfprint_dir" apply \
  "$repo_dir/patches/libfprint/libfprint-goodix53x5-usb-persist.patch"

rm -rf "$libfprint_dir/libfprint/drivers/goodix53x5"
mkdir -p "$libfprint_dir/libfprint/drivers/goodix53x5"
cp -R "$repo_dir/drivers/goodix53x5/." "$libfprint_dir/libfprint/drivers/goodix53x5/"
rm -rf "$libfprint_dir/libfprint/sigfm"

if ! cmp -s "$repo_dir/tests/test-goodix53x5-milan-synthetic.c" \
             "$libfprint_dir/tests/test-goodix53x5-milan-synthetic.c"; then
  cp "$repo_dir/tests/test-goodix53x5-milan-synthetic.c" \
     "$libfprint_dir/tests/test-goodix53x5-milan-synthetic.c"
fi

if ! cmp -s "$repo_dir/tests/test-goodix53x5-milan-state.c" \
             "$libfprint_dir/tests/test-goodix53x5-milan-state.c"; then
  cp "$repo_dir/tests/test-goodix53x5-milan-state.c" \
     "$libfprint_dir/tests/test-goodix53x5-milan-state.c"
fi

if ! cmp -s "$repo_dir/tests/test-goodix53x5-milan-runtime.c" \
             "$libfprint_dir/tests/test-goodix53x5-milan-runtime.c"; then
  cp "$repo_dir/tests/test-goodix53x5-milan-runtime.c" \
     "$libfprint_dir/tests/test-goodix53x5-milan-runtime.c"
fi

if ! cmp -s "$repo_dir/tests/test-goodix53x5-milan-runtime-seam.h" \
             "$libfprint_dir/tests/test-goodix53x5-milan-runtime-seam.h"; then
  cp "$repo_dir/tests/test-goodix53x5-milan-runtime-seam.h" \
     "$libfprint_dir/tests/test-goodix53x5-milan-runtime-seam.h"
fi

if ! grep -q "'goodix53x5'" "$libfprint_dir/libfprint/meson.build"; then
  git -C "$libfprint_dir" apply "$repo_dir/meson-integration.patch"
fi

if [ -d "$libfprint_dir/$build_dir" ]; then
  # Register overlay-defined options before assigning them in an older builddir.
  meson setup "$libfprint_dir/$build_dir" "$libfprint_dir" --reconfigure
  meson setup "$libfprint_dir/$build_dir" "$libfprint_dir" --reconfigure "${meson_options[@]}"
else
  meson setup "$libfprint_dir/$build_dir" "$libfprint_dir" "${meson_options[@]}"
fi

if [[ -n "${GOODIX_LIBFPRINT_NINJA_TARGET:-}" ]]; then
  ninja -C "$libfprint_dir/$build_dir" "$GOODIX_LIBFPRINT_NINJA_TARGET"
else
  ninja -C "$libfprint_dir/$build_dir"
fi
