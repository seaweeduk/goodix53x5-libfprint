# preprocess_init_calidata

## Identity

- Binary: `GoodixEngineAdapter.dll` 2.0.310.900, SHA-256
  `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4`.
- Export/body: `preprocess_init_calidata`,
  `0x1800030d0..0x180003171`.
- Production caller: `_InitPreProcessor_E` (`FUN_18002bfa0`) after either a
  sensor-ID mismatch or a nonzero `preprocess_load_calidata` result.

## Default-State Contract

The function reads the unsigned profile row and column globals and traverses
the complete row-major image domain. For every element it first writes Q13 unity
`0x2000` to workspace `DAT_180219670 + 0x04`, then writes zero to workspace
`DAT_180219670 + 0x9924`.

After the loops it performs these writes in order:

1. Set workspace sample count `DAT_180219670 + 0x00` to zero.
2. Clear exactly `0xa000` bytes at `DAT_18023faf8` with `memset`.
3. Set `DAT_180249af8` to zero.

It does not write the `0x0800` block at `DAT_180218e70`, the full-frame block
at `DAT_180249b00`, or profile-9 gain globals outside the workspace. The first
two blocks have no production caller beyond their otherwise unreferenced
copy-in/copy-out helpers. The `0xa000` clear starts at workspace `+0x26488` and
therefore invalidates the retained broken-level packet consumed by
`FUN_1800501d0`. On a fresh DLL, fallback classification consequently starts
without imported history, component ages, or support state. This function does
not clear the corresponding process globals or their import latch, so that
qualification does not apply to fallback within an already loaded DLL. The
final scalar has no established live read.

Profile 9 fixes the loop bounds at `88x108`. Row, column, and flattened index
arithmetic is unsigned 32-bit, so no multiplication or index overflow is
reachable. Zero rows or columns skip the plane loop but still perform the count,
block, and scalar resets.

## Return And Caller Behavior

The function allocates nothing, has no conditional error return, ignores the
logging helper's result, and returns zero after the final store. Memory faults
remain process faults rather than reported statuses.

`FUN_18002bfa0` contains a defensive nonzero-result branch that would map an
initializer failure to adapter status `0x8002`, but that branch is unreachable
from this analyzed body. Success continues to `preprocessor_init`, whose
profile-9/type-12 setup path replaces the zero setup plane with the normalized
current setup frame plus `0x1bb7`.

On the first live call, workspace sample count zero causes `FUN_1800672e0` to
clear the calibration plane and initialize all three profile-9 gain planes to
Q13 unity before they can affect rendering. Thus the initializer's entry Q13
calibration plane and the process globals' initial zero gain planes converge to
the same live-call inputs before consumption.

## Evidence

- Profile loops and interleaved plane stores: `0x1800030e2..0x18000313e`.
- Sample-count reset: `0x18000314a..0x180003152`.
- `0xa000` clear: `0x180003152..0x18000315f`.
- Final scalar reset and zero return: `0x180003164..0x180003171`.
- Production fallback calls: `FUN_18002bfa0:0x18002c501` and
  `0x18002c5ae`.

## Confidence

High for every write, loop bound, omitted field, return value, and caller
mapping.
