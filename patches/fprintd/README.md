# fprintd Milan Update Save Overlay

`1.94.5-milan-update-save.patch` targets fprintd `v1.94.5` at exact commit
`b54a007ccf58ac0ae074c7151b223f35cbd17306`. It consumes the paired
libfprint terminal update boolean and synchronously saves the already-mutated
matched print through stock `store.print_data_save()`.

Verify saves the object held in `priv->verify_data`; identify saves the exact
match returned by libfprint. Saving occurs once after the match report and
before operation-owned state is cleared. A save failure is logged and discarded
without changing authentication. No storage API, file backend, transaction,
locking, retry, or persistence test subsystem is added.

Verify a pristine pinned checkout:

```sh
./patches/fprintd/verify-update-save-patch.sh /path/to/fprintd-v1.94.5
```

Regenerate deterministically from a modified checkout at the pinned commit:

```sh
./patches/fprintd/generate-update-save-patch.sh /path/to/modified-fprintd
```

The SHA-256 sidecar is authoritative and checked before apply validation.
