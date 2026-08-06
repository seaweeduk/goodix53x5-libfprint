# Debug Data Capture Guide

This guide installs the opt-in `goodix53x5` diagnostic build and passively
captures private, replayable Milan operations. It is for local parity research
only. The release build contains none of the dump writer, diagnostic formats,
environment variables, hashes, or extra runtime fields described here.

The capture is not labeled `genuine` or `impostor`. It records opaque inputs and
the driver result so the same transaction can be compared with the canonical-
zero native Goodix implementation.

## What Capture Retains

For each fprintd operation, the debug dump retains:

- every TX-on setup candidate (`raw12-ref-txon-*`), which is the only
  replayable setup authority;
- TX-off reference frames (`raw12-ref-*`) for capture validation only;
- raw and processed enrollment or verify/identify frames;
- one owner-only runtime JSON record per attempted stage;
- ordered gallery indices, scores, decisions, selected feature, and queue
  occupancy around matching;
- hashes for input, after-match, probe, and final candidate templates;
- the relevant user's FP3 state before and after the operation;
- the bounded fprintd journal range for the operation;
- the exact installed debug-library and source identity;
- SHA-256 and size for every retained artifact.

The dump directory is persistent. Do not use `/tmp`, `/var/tmp`, or `/run`.

CLI help is available without sudo:

```sh
./tools/milan-parity/milan-parity help
./tools/milan-parity/milan-parity help finish-capture
./tools/milan-parity/milan-parity capture --help
```

## 1. Build The Paired Debug Stack

Run from the `goodix53x5-libfprint` repository as your normal user:

```sh
GOODIX53X5_DEBUG=1 ./scripts/build-milan-stack-local.sh
```

The builder uses pinned libfprint and fprintd revisions, applies the private
terminal update/save patches, builds the driver with `goodix53x5_debug=true`,
and runs the retained libfprint and Milan tests once. It publishes a verified
payload under `~/.local/state/goodix53x5-milan/builds/` without changing the
running service. Do not install it until the old campaign has been finalized
and the old enrollments have been deleted in step 2.

## 2. Finalize The Old Campaign And Delete Enrollments

Finish or snapshot the old campaign before changing the service, enrollment
state, or capture path. To finish it, set its existing dump path and freeze the
service while the final inventory is written:

```sh
old_dump="OLD_DUMP_DIRECTORY"

sudo systemctl stop fprintd.service
./tools/milan-parity/milan-parity finish-capture --dump-dir "$old_dump"
sudo rm /etc/systemd/system/fprintd.service.d/99-goodix53x5-parity-capture.conf
sudo systemctl daemon-reload
sudo systemctl start fprintd.service
```

At this point the old campaign is closed and its files must not receive another
operation. Delete the current user's existing enrollments now, while the old
stack is still installed and capture is disabled:

```sh
fprintd-delete "$USER"
```

This deletion must happen before installing the new debug stack. A successful
deletion may remove `/var/lib/fprint/$USER` entirely. The capture tool records a
missing pre-enrollment store as an empty baseline for enrollment only; it still
requires storage to exist after enrollment and throughout verify/identify.

## 3. Create A Purpose-Named Campaign

Use a short purpose name rather than a timestamp. Increment the final revision
only when intentionally starting another incompatible campaign:

```sh
campaign_id="milan-inplace-learning-stock-save-v1"
campaign_parent="$HOME/dev/goodix-fp-dump/campaigns"
campaign_root="$campaign_parent/$campaign_id"
dump="$campaign_root/debug-dump"

test ! -e "$campaign_root"
install -d -m 0700 "$campaign_parent"
mkdir -m 0700 "$campaign_root"
mkdir -m 0700 "$dump"
printf 'Campaign root: %s\nDump directory: %s\n' "$campaign_root" "$dump"
```

Do not reuse these paths for another driver build. Files written directly by
fprintd are root-owned mode `0600` until the campaign is finalized.

## 4. Configure Persistent Capture And Install

Write the capture environment into the persistent systemd drop-in. It remains
active across service restarts and reboots until explicitly removed when
returning to the release stack:

```sh
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

Installation atomically switches `/opt/goodix53x5-milan`, preserves
`StateDirectory=fprint`, and restarts fprintd. `GOODIX53X5_DUMP_PROBES=all` and
`GOODIX53X5_DUMP_TEMPLATES=1` make successful operations self-contained.
TX-on setup candidates are always dumped when `GOODIX53X5_DUMP_DIR` is set;
`raw12-ref-*` TX-off frames cannot be used as replay setup.
`ProtectHome=false` and `ReadWritePaths` permit the system service to write to
the persistent path below the user's home directory.

## 5. Verify And Record The Installed Build

The managed stack manifest must say that this is a debug build:

```sh
grep '^GOODIX53X5_DEBUG=1$' /opt/goodix53x5-milan/manifest/build.env
strings /opt/goodix53x5-milan/lib/libfprint-2.so.2.0.0 |
  grep 'goodix53x5-runtime-debug/v2'
```

Both commands must print a match. If either fails, stop. Do not collect with a
release or stale library.

Runtime debug v2 adds a per-service-device capture session UUID. Current parity
tooling still reads legacy v1 records, but it requires the UUID on v2 records
and will not join generation preludes across different sessions.

Record the installed library identity in the new campaign root:

```sh
./tools/milan-parity/milan-parity build-manifest \
  --repo "$PWD" \
  --library /opt/goodix53x5-milan/lib/libfprint-2.so.2.0.0 \
  --output "$campaign_root/driver-build.json" \
  --debug

printf 'Build manifest: %s\n' "$campaign_root/driver-build.json"
```

The collector rehashes this path and verifies that the running fprintd process
mapped the same library. A caller-provided `--debug` flag alone is not trusted.

## 6. Use The Driver Normally

Start from the empty enrollment state created in step 2. Enroll whichever
fingers you want, whenever you want, and use fingerprint authentication normally.
There is no prescribed operation order or per-operation capture command. The
persistent service drop-in writes enrollment, verify, identify, retry, failure,
and learning artifacts into `$dump` across service restarts and reboots.

Do not install a different libfprint build into the same campaign. Finish or
snapshot this campaign before switching back to a release build or installing
another debug build.

## 7. Inspect A Completed Campaign

For a passive dump collected during normal laptop use, finish collection with
one command:

```sh
./tools/milan-parity/milan-parity finish-capture --dump-dir "$dump"
```

This asks for sudo, copies the current user's final saved fingerprints into the
dump, returns ownership of all collected files to the user, locks permissions,
and writes `capture-finished.json` with every file's size and SHA-256. Users do
not need to copy saved fingerprint files after each unlock.

The finalized debug dump contains the complete passive artifact stream,
`saved-enrollments-end/`, and `capture-finished.json`. Read and analyze the
owner-owned finalized dump rather than live root-owned files.

Do not publish the campaign, template hashes, images, FP3 files, journal range,
or generated native output. Do not add them to Git or Git LFS.

### Validate A Preliminary Snapshot

Do not run `finish-capture` while a multi-day campaign is still active. Instead,
briefly stop fprintd and make a private point-in-time copy:

```sh
snapshot="$campaign_root/snapshots/pre-release-validation-v1"
test ! -e "$snapshot"
mkdir -p -m 0700 "$(dirname "$snapshot")"
sudo systemctl stop fprintd.service
sudo cp -a -- "$dump" "$snapshot"
sudo systemctl start fprintd.service
sudo chown -R "$USER:$(id -gn)" "$snapshot"
chmod -R u+rwX,go-rwx "$snapshot"
```

The original directory remains the active collection target. Validate the
snapshot, not the live directory, so a new fingerprint operation cannot change
the inventory during replay.

Configure the private native authority once per shell, then validation is one
command:

```sh
export MILAN_PARITY_DLL=/private/path/GoodixEngineAdapter.dll
export MILAN_PARITY_DLL_SHA256=6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4
export MILAN_PARITY_WINEPREFIX=/private/path/to/existing-wine-prefix

./tools/milan-parity/milan-parity validate-dump --dump-dir "$snapshot"
```

`validate-dump` prints the complete operator verdict directly. It includes the
number of operations compared to the DLL, study-action and score coverage,
queue transitions, learned-candidate continuity, and every skipped-check reason
grouped with a count. The JSON report remains available for detailed evidence;
`jq` is not required to understand the result.

Example:

```text
milan_parity_dump=pass operations=15 checks=35/0/10
native_parity=pass compared=6 failed=0 unavailable=9
native_coverage actions=5:6 scores=25,40,42,48,52,88 queues=0->1:6
learning_continuity=partial reobserved=5 pending=1 failed=0
skipped_checks:
  8 native-parity: enrollment authority requires a complete twelve-stage chain
  1 candidate-reobserved: no later gallery contains the exact learned candidate
  1 native-parity: required setup, live frame, or gallery template is missing
report=/home/user/.local/state/milan-parity/reports/dump-<inventory>.json
```

An unavailable enrollment DLL check is expected: controlled-zero enrollment
cannot be run directly under Wine. Matching, study, learning, append/replacement,
and queue behavior are still checked against the direct DLL authority whenever
the dump contains their required artifacts.

## Troubleshooting

### The dump directory stays empty

Confirm the debug stack, service environment, sandbox override, and service log:

```sh
./scripts/status-milan-stack-local.sh --installed
grep '^GOODIX53X5_DEBUG=1$' /opt/goodix53x5-milan/manifest/build.env
systemctl show fprintd.service --property=Environment --value
systemctl cat fprintd.service
journalctl -u fprintd.service -n 100 --no-pager
```

The most common cause is that the release stack is still installed. Building a
debug payload does not install it; `sudo ./scripts/install-milan-stack-local.sh`
is a separate step.

### The collector says the loaded library differs

Restart fprintd after installing, then regenerate `driver-build.json` from the
installed `/opt` library. Do not reuse a manifest from the build tree or a prior
installation.

### The collector says raw/processed probes are missing

Confirm that the service environment contains:

```text
GOODIX53X5_DUMP_PROBES=all
```

Then restart fprintd and use a new empty dump/campaign directory.

### The collector reports multiple operation identities

Another fingerprint operation occurred during capture. Keep the failed bundle
for diagnosis, create a new campaign destination, and rerun while avoiding
screen unlock or another fprintd client.

## Return To The Release Stack

Build and install a verified release stack:

```sh
GOODIX53X5_DEBUG=0 ./scripts/build-milan-stack-local.sh
sudo ./scripts/install-milan-stack-local.sh
./scripts/status-milan-stack-local.sh --installed
```

Then remove the collection-only service override:

```sh
sudo rm /etc/systemd/system/fprintd.service.d/99-goodix53x5-parity-capture.conf
sudo systemctl daemon-reload
sudo systemctl restart fprintd.service
```

Do not delete the dump directory or completed campaigns until their manifests
have been validated and the required cases admitted into the private corpus.
