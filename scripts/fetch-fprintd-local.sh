#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$script_dir/.." && pwd)"
# shellcheck source=scripts/lib/milan-stack-common.sh
source "$script_dir/lib/milan-stack-common.sh"

for command in git flock timeout; do
  milan_require_command "$command"
done
milan_reject_ephemeral_root "GOODIX_MILAN_STACK_ROOT" "$MILAN_STACK_ROOT"

source_root="${GOODIX_MILAN_FPRINTD_SOURCE_ROOT:-$MILAN_STACK_ROOT/sources}"
sibling_source="$(cd "$repo_dir/.." && pwd)/goodix-fp-dump/derived/milan-stack/sources/fprintd-v1.94.5"
if [[ -n "${GOODIX_MILAN_FPRINTD_SOURCE:-}" ]]; then
  source_dir="$GOODIX_MILAN_FPRINTD_SOURCE"
elif [[ -d "$sibling_source/.git" ]]; then
  source_dir="$sibling_source"
else
  source_dir="$source_root/fprintd-v1.94.5"
fi

expected_revision="$MILAN_FPRINTD_REVISION"
if [[ -n "${GOODIX_MILAN_TEST_FPRINTD_REVISION:-}" ]]; then
  expected_revision="$GOODIX_MILAN_TEST_FPRINTD_REVISION"
fi

if [[ -e "$source_dir" ]]; then
  milan_verify_git_pristine "$source_dir" "$expected_revision" "fprintd"
  milan_note "verified pinned pristine fprintd source: $source_dir"
  exit 0
fi

milan_reject_ephemeral_root "fprintd source root" "$source_root"
[[ "$(dirname "$(readlink -m "$source_dir")")" == "$(readlink -m "$source_root")" ]] ||
  milan_die "new fprintd source must be an immediate child of its source root"
mkdir -p "$source_root"
exec 9>"$source_root/.fetch-fprintd.lock"
flock -n 9 || milan_die "another fprintd fetch is active"

staging="$source_root/.fprintd-v1.94.5.fetch.$$"
cleanup() {
  [[ ! -e "$staging" ]] || milan_safe_remove_tree "$staging" "$source_root"
}
trap cleanup EXIT
milan_run_stage "clone pinned fprintd repository" git clone --no-checkout \
  "${GOODIX_MILAN_FPRINTD_URL:-https://gitlab.freedesktop.org/libfprint/fprintd.git}" "$staging"
milan_run_stage "checkout exact fprintd revision" git -C "$staging" checkout --detach "$expected_revision"
milan_verify_git_pristine "$staging" "$expected_revision" "fprintd"
mv "$staging" "$source_dir"
trap - EXIT
milan_note "fetched and verified fprintd $expected_revision at $source_dir"
