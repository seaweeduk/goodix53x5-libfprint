# Debug Data Capture Guide

This guide installs the opt-in `goodix53x5` diagnostic build and captures
private Milan operations into a persistent, restart-safe dump. The release
build contains none of this dump writer, runtime schema, debug build/source
identity markers, or diagnostic environment.

Current collection and validation accept exactly runtime schema
`goodix53x5-runtime-debug/v3` with print schema 4. Each campaign uses one current
schema and one debug build identity; do not mix records from different captures
or builds.

## What The Dump Retains

The debug dump records:

- every TX-on setup candidate used as replay setup authority;
- raw and processed enrollment, verify, and identify frames;
- canonical runtime records with session/action/epoch, unique chronology, and
  the compile-time build ID;
- raw native probe templates and exact active/partition counts;
- strict `.bin` setup, live, processed, probe, input, after-match, and final
  candidate artifacts with exact size/hash descriptors;
- gallery scores, decisions, winner identity, natural queue observations, and
  preprocess/extraction/study lifecycle state;
- exact nullable `preprocess_status_i32`, with null for no attempt and the exact
  int32 return for every attempted preprocessing call.

Enrollment retries may emit repeated attempts for the same stage. The stage
number identifies what was attempted; chronology uniquely orders every attempt.
Declared gallery, evaluated/valid/invalid, probe partition, and artifact counts
are checked exactly.

One-operation `capture` bundles additionally retain saved fingerprints before
and after the command, the bounded fprintd journal range, the exact driver-build
manifest, and SHA-256/size descriptors. A finalized passive dump retains the
final saved fingerprints and a complete inventory.

All biometric artifacts, hashes, journals, reports, DLL output, and saved
fingerprints are private. Do not publish them or add them to Git or Git LFS.
Use persistent owner-only paths, never `/tmp`, `/var/tmp`, or `/run`.

## 1. Build Without Installing

From this repository, as your normal user:

```sh
GOODIX53X5_DEBUG=1 ./scripts/build-milan-stack-local.sh
```

The builder publishes a verified payload below
`~/.local/state/goodix53x5-milan/builds/` without changing the running service.
Do not install it until any existing current campaign has been finalized and
existing enrollments have been deleted as described next.

Every debug compilation receives a unique random 256-bit build ID. A separate
deterministic production source identity stays the same when the production
source and fixed source-identity inputs are unchanged.

## 2. Finalize An Existing Campaign

Before replacing an installed build, changing enrollment state, or selecting a
new dump path, finalize any active current campaign:

```sh
existing_dump="/private/EXISTING_CAMPAIGN/debug-dump"

sudo systemctl stop fprintd.service
./tools/milan-parity/milan-parity finish-capture --dump-dir "$existing_dump"
sudo rm -f /etc/systemd/system/fprintd.service.d/99-goodix53x5-parity-capture.conf
sudo systemctl daemon-reload
sudo systemctl start fprintd.service
```

`finish-capture` copies the final saved prints, takes ownership, locks down the
tree, and writes schema `milan-parity-finished-capture/v2`. The finalized dump
is closed and must not receive another operation.

After finalizing any active campaign, but before installing the new debug build,
delete the current user's existing enrollments:

```sh
fprintd-delete "$USER"
```

This ordering is mandatory: finalize the active campaign, delete existing
enrollments, then install the new debug build. A successful deletion may remove
`/var/lib/fprint/$USER` entirely.

If there is no active campaign, still delete existing enrollments before the
new installation. Use `save-enrollments` first only if a separate private
snapshot is required:

```sh
./tools/milan-parity/milan-parity save-enrollments \
  --output /private/ENROLLMENT_ARCHIVE
```

## 3. Create Fresh Paths

Use a new purpose-named directory. Do not hardcode or reuse a previous campaign
name, dump, build manifest, or operation bundle:

```sh
campaign_id="PURPOSE-vN"
campaign_parent="$HOME/dev/goodix-fp-dump/campaigns"
campaign_root="$campaign_parent/$campaign_id"
dump="$campaign_root/debug-dump"
manifest="$campaign_root/driver-build.json"

test ! -e "$campaign_root"
install -d -m 0700 "$campaign_parent"
mkdir -m 0700 "$campaign_root"
mkdir -m 0700 "$dump"
```

Each campaign is tied to one random debug build ID and its deterministic
production source identity.

## 4. Install A Persistent Restart-Safe Drop-In

Create the fresh drop-in with the new dump path before installing the debug
stack:

```sh
sudo install -d -m 0755 /etc/systemd/system/fprintd.service.d
sudo tee /etc/systemd/system/fprintd.service.d/99-goodix53x5-parity-capture.conf >/dev/null <<EOF
[Service]
Environment=G_MESSAGES_DEBUG=libfprint-goodix53x5
Environment=GOODIX53X5_DUMP_DIR=$dump
Environment=GOODIX53X5_DUMP_PROBES=all
Environment=GOODIX53X5_DUMP_TEMPLATES=1
Environment=GOODIX53X5_LOG_TIMING=1
Environment=GOODIX53X5_LOG_DIAGNOSTICS=1
ProtectHome=false
ReadWritePaths=$dump
EOF

sudo systemctl daemon-reload
sudo ./scripts/install-milan-stack-local.sh
./scripts/status-milan-stack-local.sh --installed
systemctl show fprintd.service --property=Environment --value
```

The drop-in persists across fprintd restarts and reboots. Do not remove it until
the capture is deliberately retired. `GOODIX53X5_DUMP_PROBES=all` and
`GOODIX53X5_DUMP_TEMPLATES=1` are required for full exact parity. TX-on setup
candidates are emitted whenever the dump directory is configured; TX-off
reference frames are not replay setup.

## 5. Create The Exact Build Manifest

First confirm the installed library is the current debug build:

```sh
grep '^GOODIX53X5_DEBUG=1$' /opt/goodix53x5-milan/manifest/build.env
strings /opt/goodix53x5-milan/lib/libfprint-2.so.2.0.0 | \
  grep 'goodix53x5-runtime-debug/v3'
```

Both checks must match. Then create a fresh manifest from the installed library:

```sh
./tools/milan-parity/milan-parity build-manifest \
  --repo "$PWD" \
  --library /opt/goodix53x5-milan/lib/libfprint-2.so.2.0.0 \
  --output "$manifest" \
  --debug
```

The `milan-parity-driver-build/v2` manifest records the random compile-time
build ID, deterministic production source identity, source commit, runtime
schema, fixed profile-9 policy, and the installed library's absolute path,
exact byte size, and SHA-256. Manifest creation rehashes the library and verifies
both embedded identities against the repository.

Live `capture` rehashes the manifest path, verifies the embedded build and
source identities, and confirms that fprintd mapped that exact library. Later
`validate-dump` treats the captured path/hash/size as sealed evidence: the path
does not need to exist or still match. Installing a fix, another debug build, or
the release stack therefore does not invalidate an investigation snapshot.
Runtime build IDs still reject a mixed-build dump or a runtime/manifest
mismatch exactly. Do not reuse one manifest to capture another build.

## 6. Capture Normal Use

Enroll fresh fingers and use fingerprint authentication normally. The
persistent drop-in writes to `$dump` across service restarts and reboots. Do not
run further live capture into this campaign after installing another library;
existing snapshots remain valid for validation through their sealed manifest.

For a deliberately bounded command, `capture` can package exactly one operation
from the same live dump:

```sh
operations="$campaign_root/operations"
mkdir -m 0700 "$operations"

./tools/milan-parity/milan-parity capture \
  --campaign "$operations/verify-001" \
  --dump-dir "$dump" \
  --build-manifest "$manifest" \
  -- fprintd-verify "$USER"
```

The destination must not already exist. Files added during the command must
identify exactly one new runtime session/action/epoch. The bundle then snapshots
the complete bounded post-operation dump, not only those new files, so earlier
session and generation dependencies remain available. It writes
`milan-parity-capture/v3`.

## 7. Validate A Restart-Safe Snapshot

Do not validate the live directory while fprintd can change it. Stop the
service briefly, copy the dump to a new owner-only path, then resume collection:

```sh
snapshot="$campaign_root/snapshots/VALIDATION_SNAPSHOT"
test ! -e "$snapshot"
mkdir -p -m 0700 "$(dirname "$snapshot")"
sudo systemctl stop fprintd.service
sudo cp -a -- "$dump" "$snapshot"
sudo systemctl start fprintd.service
sudo chown -R "$USER:$(id -gn)" "$snapshot"
chmod -R u+rwX,go-rwx "$snapshot"
```

Read `operation_id` from the current v3 runtime records and select each operation
explicitly as `SESSION/ACTION/EPOCH`. Nonselected operations are reported as
skipped. A selected operation that is unavailable for any reason fails.

Configure the approved DLL and existing Wine prefix:

```sh
export MILAN_PARITY_DLL=/private/GoodixEngineAdapter.dll
export MILAN_PARITY_DLL_SHA256=APPROVED_LOWERCASE_SHA256
export MILAN_PARITY_WINEPREFIX=/private/existing-wine-prefix
```

Validate captured behavior against native:

```sh
./tools/milan-parity/milan-parity validate-dump \
  --dump-dir "$snapshot" \
  --build-manifest "$manifest" \
  --operation 01234567-89ab-4cde-8123-456789abcdef/identify/58 \
  --operation 01234567-89ab-4cde-8123-456789abcdef/verify/59
```

Validate rebuilt current source against native on the same selected operations:

```sh
GOODIX53X5_DEBUG=0 ./scripts/build-local.sh

./tools/milan-parity/milan-parity validate-dump \
  --dump-dir "$snapshot" \
  --build-manifest "$manifest" \
  --operation 01234567-89ab-4cde-8123-456789abcdef/identify/58 \
  --compare-current \
  --repo "$PWD"
```

Run the first command explicitly in a clean checkout. It creates
`.build/libfprint/builddir`, which `--compare-current` requires. The Milan stack
builder does not create this checkout-local support build.

Native execution is always natural identify/study. Captured queue occupancy is
compared as an observation; it is never supplied as queue input. Validation
compares exact processed image, probe, every gallery after-match template,
final candidate, natural queue transitions, and lifecycle outputs. The report
identifies the capture build/manifest/inventory, approved DLL and native runner,
selectors, optional current source, every operation, and every exact difference
or unavailable reason.

Lifecycle `completed` means that the phase succeeded. Failed, cancelled, and
retry records remain structurally valid when unavailable outputs and artifacts
are null according to their actual lifecycle. Such records do not poison
nonselected operations. Native and current runners report actual lifecycle
observations for comparison; they do not assume completion from the requested
operation. A nonzero preprocessing return carries zero quality and coverage and
no downstream artifacts or lifecycle attempts. Retry-only selection/replay is
not implemented yet.

## 8. Finish The Current Capture

When collection is complete, stop fprintd so no operation can race finalization:

```sh
sudo systemctl stop fprintd.service
./tools/milan-parity/milan-parity finish-capture --dump-dir "$dump"
sudo rm -f /etc/systemd/system/fprintd.service.d/99-goodix53x5-parity-capture.conf
sudo systemctl daemon-reload
sudo systemctl start fprintd.service
```

Retain the finalized owner-only dump, `driver-build.json`, and validation
reports together. The manifest's captured library identity remains sealed
evidence; future validation does not require retaining or reinstalling that
absolute library path.

## Troubleshooting

### The dump stays empty

```sh
./scripts/status-milan-stack-local.sh --installed
grep '^GOODIX53X5_DEBUG=1$' /opt/goodix53x5-milan/manifest/build.env
systemctl show fprintd.service --property=Environment --value
systemctl cat fprintd.service
journalctl -u fprintd.service -n 100 --no-pager
```

Building does not install. Confirm the current debug stack and fresh persistent
drop-in are both active.

### The build identity differs

Do not regenerate a manifest to conceal a mixed campaign. Confirm the dump is
fresh, manifest creation and live capture inspected the intended library, and
fprintd mapped it during capture. A runtime record with another random build ID
belongs in another campaign. A later change or removal at the captured absolute
library path is permitted because validation uses the sealed manifest evidence.

### The runtime schema is rejected

The checker accepts exactly `goodix53x5-runtime-debug/v3` with print schema 4.
Install the current debug build and create a fresh campaign, manifest, drop-in,
and dump.

### A selected operation is unavailable

A contract-valid failed, cancelled, or retry record may have nullable artifacts
and does not affect nonselected operations. If its operation is selected and
cannot supply the required replay projection, enrollment, cancellation, empty
or invalid galleries, missing artifacts, or incomplete generation chronology
fail by design. Keep the report and original dump for investigation; do not
remove the selector to turn an active failure into an apparent pass.

## Return To The Release Stack

Retire the current capture first, then build and install the release stack:

```sh
GOODIX53X5_DEBUG=0 ./scripts/build-milan-stack-local.sh
sudo ./scripts/install-milan-stack-local.sh
./scripts/status-milan-stack-local.sh --installed
```

Ensure the capture drop-in has been removed, reload systemd, and restart
fprintd. Retain finalized private dumps and their exact build manifests for the
duration of the investigation. The release installation may occupy the captured
library path while those dumps are validated.
