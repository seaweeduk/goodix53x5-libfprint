# templateDelete

## Identity

- DLL: `GoodixEngineAdapter.dll`
- DLL SHA-256: `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`
- Address/body: `0x180002290..0x18000230a`.
- Role: exported owner destructor for a public template handle.

## Call Graph

- Production callers include `FUN_180011530`, `FUN_18002bbf0`, and
  `FUN_18002dde0`.
- Calls `FUN_180037860` to destroy the internal live template, then frees the
  eight-byte public handle.

## Recovered Behavior

For a nonnull handle with a nonnull first field, the export destroys the entire
internal template graph and frees the handle allocation. It does nothing for a
null handle or a handle with a null first field. The oracle therefore owns the
handle returned by `templateUnPack` and correctly releases it exactly once at
the end of `run_identify`.

The null-internal case does not free the otherwise nonnull eight-byte wrapper.
Valid public handles never use that state. Decoder failure frees its wrapper
itself after `FUN_18003e3a0` has deep-cleaned any partially allocated live
object.

## Confidence And Open Questions

- Confidence: high for ownership and destruction order.
- The export does not null the caller's handle variable; callers must not reuse
  it after deletion.
