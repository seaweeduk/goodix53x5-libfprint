# Libfprint Update Result Overlay

`libfprint-update-result.patch` is an additive Patch A overlay for libfprint
`v1.94.10` at exact commit
`0c97a47d8ef405cd577b87058c1e89cae9d242e7`.

The patch is the narrow private contract shared by the repository's Milan
driver and pinned fprintd build. The driver mutates a validated matched raw
print in place, reports matches through the stock APIs, and supplies one update
boolean only at terminal verify or identify completion. Existing finish APIs
remain wrappers that ignore the boolean. One focused test covers positive,
no-match, and terminal-error propagation.

`scripts/build-local.sh` verifies and applies this patch immediately after the
pinned checkout and before copying the driver overlay or configuring Meson.
The build fails deterministically on a wrong revision, tracked source changes,
or patch context drift. Untracked build/overlay files are ignored because the
local checkout intentionally retains them between builds. To perform the same
check manually:

```sh
./patches/libfprint/verify-update-result-patch.sh /path/to/pristine/libfprint
```

Regenerate it deterministically from a modified worktree at the pinned commit:

```sh
./patches/libfprint/generate-update-result-patch.sh /path/to/modified/libfprint
```

The generator writes a base-revision header into the patch. The verifier
enforces that revision before running `git apply --check`; `git apply` alone
does not enforce patch metadata.
