# usbinterface.dll FUN_180005420

## Identity

- Address: `0x180005420`
- Logged name: `Milan_GetFdtBaseWithTX`
- Role: acquire a profile-9 manual FDT sample and return its raw 24-byte
  response.
- Installation: `FUN_18000450c` installs it at hardware callback slot `+0x160`
  at `0x180004586..0x18000458d`.

## Command Contract

The function copies the retained manual-base store at
`0x180060778..0x18006078f` into a local command buffer before issuing category
`3`, command `3`, with checksum and a 500 ms timeout. Its operation byte is
`0x0d` when the TX-enable argument is nonzero and `0x8d` when it is zero; byte
one is `0x01`, followed by the 24 retained base bytes.

On a successful transport response it copies the raw manual FDT result from
`0x180060718..0x18006072f` to the caller and returns zero. A failed command
returns `-1`.

`FUN_180017ec0` forwards to `FUN_180019ec8` (`ChangeMode`). Its manual-FDT
branch calls `FUN_180018dd8` with ACK timeout 500 ms, response timeout 500 ms,
and response-event selector 10. If that attempt returns zero, it retries the
same command once; only failure of both attempts reaches this callback's
`-1` result. A successful first attempt is not repeated.

`FUN_180018dd8` resets the selected response event before sending, then waits
for it in `WaitForSingleObject` calls with 50 ms timeouts, up to the supplied
response timeout. A result other than `WAIT_TIMEOUT` ends that loop immediately;
50 ms is not an unconditional delay between FDT commands.

## Reverse-Event Relationship

`FUN_180005b80` copies the prior hardware down-arm base into the manual-base
store before replacing the down-arm base for every reverse IRQ. Up-event parsing
does not replace the manual store. A later `MilanHV_Down_procedure` invokes this
function for its immediate TX-off manual reading, so the reverse snapshot is a
persisting command input rather than comparison-only state.

See `usbinterface-profile9-fdt-event-loop.md` for parser/handler ordering,
`usbinterface-FUN_1800048d0.md` for base transformation and store consumers,
and `usbinterface-FUN_180015c60.md` for the full-acquisition owner that uses
this callback and later replaces the base stores.
