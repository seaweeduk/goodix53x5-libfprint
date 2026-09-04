# templateStudy

## Identity And Inputs

- Body: `0x180001f70..0x180002094`.
- Role: consume the retained image-identification probe and expose the study
  action without packing or replacing the live gallery handle.
- Matched live gallery: global `0x18024e548`.
- Independently owned retained probe: global `0x18024ebe8`.
- Recognition evidence: global `0x18024e550`, size `0x698`.
- Argument: pointer to a 32-bit action output.

The gallery is the same object mutated during identification. There is no
intervening unpack or copy. `identifytemplate` does not publish this retained
global state and cannot substitute for the image-identification caller.

## Dispatch And Output

At `0x180001fd5..0x180001fd9`, local action/index initialize to `0/-1`.
The tests at `0x180001fde..0x180001fe9` enter `FUN_180044fc0` only for a
nonnull gallery and nonzero global `0x18024ebd8`. That global is exactly
evidence `+0x688`, not a separate gate. The call at `0x180002006` supplies the
unchanged gallery, retained probe, evidence block, local result, and mode one.

For profile 9/type 12, the dispatcher outputs only actions `0..5`:

- `0`: no positive study update.
- `1`: append.
- `2`: replacement without relation installation.
- `3`: retained geometric-fallback replacement.
- `4`: retained primary replacement.
- `5`: at least one successful queued selector after the primary mutation.

The export writes the action unchanged to its caller. It does not remap three
to four, suppress three, or invent six. The dispatcher's ordinary return is
zero; a hypothetical nonzero return follows `0x180002011..0x18000203d`, copies
the local action, destroys the retained probe, and returns that status.

With a valid gallery and gate zero, dispatch is skipped, action zero is written,
and the retained probe is still consumed. This path performs no dispatcher
retained refresh, normalization, queue mutation, ordering, or tail increment.
Dispatcher-entered action zero is distinct: earlier live mutations can occur
before the dispatcher returns zero action.

## Ownership And Finalization

On normal valid-gallery completion, `0x18000206f..0x180002076` passes the
owning probe-global address to `FUN_180037b10`, which destroys the probe and
clears the global. The probe is single-use. Neither positive nor zero action
destroys the gallery here. A late-enqueued independent copy remains owned by
the gallery after original-probe cleanup.

All primary mutations, queued rematches, queued selector mutations, and queue
consumption operate on that same gallery before the dispatcher's one final
accepted-evidence order/tail phase. Selector normalization is immediate, not
deferred to export or packing. Queue shutdown occurs after continuation and
before the final order/tail phase. See `FUN_180044fc0.md` and
`FUN_18005d330.md` for the exact predicates and order.

This export calls neither `templateGetPackedSize` nor `templatePack` for any
action. `FUN_18002ba60` invokes it only when the successful-match latch equals
one, and queries new packed size only for positive action. `FUN_18002bbf0`
packs the selected live handle only for positive action and returns that action
unchanged. Its extra `identifyUpdate` call for actions greater than one is a
literal zero-return no-op at `0x1800020a0..0x1800020a2`. Thus actions `1..5`
share the positive candidate boundary, including action five.

Action zero publishes no template update even if transient gallery or queue
state changed. The adapter subsequently destroys all cached candidate handles
and their queue owners. Queue bodies and ranks are not serialized; only queue
state/counter metadata persists. The queue does not survive as service state
across these adapter transactions.

The queue producer and this export use opposite values of the same evidence
word. Match-time insertion requires evidence `+0x688 == 0`; this export calls
the dispatcher only when `+0x688 != 0`. A queue owner inserted by the current
match therefore cannot be consumed by the current study. Action five requires
a queue already present on a retained live handle from an earlier match/study
operation. The adapter's unconditional post-action-zero handle cleanup removes
that owner, and the next adapter transaction freshly unpacks ranks as `-1`.
Consequently action five is reachable only for a caller that deliberately
retains and reuses the live DLL handle across the intervening action-zero study;
it is not reachable through the ordinary adapter persistence lifecycle.

## Invalid Inputs And Failure

The null-output branch is not a safe `0x81` return: at `0x180001fa2` it writes
through the null output pointer before the nominal cleanup and status return.
Likewise, the null-gallery branch skips dispatch but reaches the unconditional
gallery feature-count load at `0x180002066`, before normal probe cleanup.
These access-violation paths must not be described as successful cleanup exits.

For ordinary valid inputs there is no rollback: matcher and study helpers mutate
the retained gallery directly. The export preserves that object for adapter
packing. Adapter pack-failure behavior is separately owned by
`FUN_18002bbf0.md`; export completion alone is not proof of publication.
