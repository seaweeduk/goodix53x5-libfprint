# usbinterface.dll FUN_180007c84

## Identity and call boundary

`usbinterface.dll!0x180007c84`, logged `Milan_DynamicAdjustDac`, owns the
profile-9 successful-read adjustment wrapper. Its arithmetic callee is
`0x1800115a4`. This note owns both contracts. The current counterpart is
`drivers/goodix53x5/device/calibration.c:goodix_device_adjust_dac`, with
`goodix_dynamic_dac_mean` and `goodix_dynamic_dac_step` owning selection and
register arithmetic. `device/scan.c:goodix_capture_ssm_handler` invokes it after
successful command completion and raw decode, before capture-ready publication.
`device/commands.c` consumes `calib.dac_h` for the next live image command;
`device/{auth,enroll}.c` copies the post-read word to runtime metadata.
`milan/runtime.c:goodix_milan_runtime_build_probe` projects the low calibration
bytes at extraction, independently of that full-word metadata owner.

Inputs are decoded live pointer, byte enable, and writable unsigned-16 DAC
pointer. Zero enable returns without reading frames or changing DAC/history.
Enabled calls form `R = u16(dac << 4) | 8`, obtain reference pointer
`HAL+0x248`, dimensions from unsigned bytes `HAL+0x1f0/+0x1f1`, and invoke
`0115a4(live, reference, rows, columns, &R, T, D)`. `T` is
`u16((seed_temperature != 0 ? seed_temperature : 128) << 4)`;
`D` is the seeded default DAC. The wrapper ignores no error status: the callee
returns void and always writes its register result. It then stores `R >> 4`
through the DAC pointer, with no additional nine-bit mask.

`007c74` writes only unsigned-16 seed temperature `0x180060cb8` and default
DAC `0x180060cbc`. `004a40` calls it after OTP validation/field selection,
including the validation-failure continuation, but not the initial OTP-read
failure. The source operands are `0x1800606fa` and `0x18005e1c0`, not the
possibly adjusted current DAC. It does not reset adjustment history.
After successful ordinary OTP selection, temperature is zero or `otp[23]+1`
for nonzero `otp[23]`, hence zero or `2..256`. Default DAC is the retained
fallback (`151` initially) or the selected nine-bit OTP DAC. The shifted
temperature denominator is consequently nonzero on this path. Current DAC
can differ from that seed after live adjustment; passing current DAC as the
default would move the native fixed limits.

## Arithmetic and register widths

The following describes `0115a4` at `0x1800115a4..0x180011b9a`.
`u16/u32` denote truncation; `s32` interprets a 32-bit pattern as signed.
Profile 9 supplies 88 rows and 108 columns. Both frame planes are read as
**signed 16-bit** values, including their border pixels in the first pass.
Ordinary decoded sensor pixels occupy `0..4095`.

All integer accumulations and products are 32-bit wrapping operations unless
explicitly stated otherwise. Integer signed divisions truncate toward zero.
The native denominator is binary64 `double(T) * 0.283`; the other binary64
constants are `0.25`, `0.4`, and `0.95` at `0x180036d60..0x180036d7f`.

### First pass and native unowned stack dependency

For all `N=rows*columns` pixels, let `L=s16(live[i])`, `B=s16(reference[i])`,
and `d=B-L`. Accumulate separate signed sums/counts for `d > 3800` and
`d <= 3800`; also count `L > B`.

There is a native partial-stack-write dependency, not an inferred parameter
type: `007c84:007d02` writes **word** `[rsp+0x28]` for `T`. After reading that
word for the floating denominator, `0115a4:011617` overwrites the same slot's
low word with `R`, then `011633` reads the whole **dword**. Its upper word has
not been initialized by either function. Define that upper word as `A` and
`Q=(A<<16)|R`. The first-pass positive-difference counter actually starts at
`Q`, not zero: `P=u32(Q + count(L>B))` (`011685..011697`).

If the `d>3800` group is nonempty, the mask threshold is
`C = 3800 - trunc((trunc(sum_high/count_high) - mean_low)/4)`, where
`mean_low=trunc(sum_low/count_low)` when nonempty and zero otherwise. This
threshold is independent of `A`, but `P` is not. If the high group is empty,
`C=s32(Q)` (`011703..01171a`), not zero. There is no initialization of the
upper word in the wrapper's local stack allocation. The unused high bits of
the temperature argument therefore become mask inputs; they are not part of
the temperature seed's defined value.
The preceding successful-read copy `0074bc:007651 -> 02e4e0` does not
initialize this wrapper stack slot; its copy body has no stack stores.

### Interior mask, mean and admission

Only rows `1..rows-2` and columns `1..columns-2` enter the second pass.
Count `nz` for `L != 0`. Select a pixel exactly when:

```text
L <= 3800 || (B-L >= C && C != 0 &&
              (s32(u32(P*10)) <= N || L <= B))
```

Comparisons of pixels/differences/threshold and `P*10` are signed. Selected
sum is a wrapping unsigned-32 accumulation of signed `L`; selected count is
unsigned-32. `m = count ? unsigned_sum / count : 0` uses unsigned division
(`01181a..011823`). Zeros can be selected and contribute to count even though
they do not contribute to `nz`.

Let `area=(rows-2)*(columns-2)`. Compute `nz_ratio=float(nz)/float(area)`
and `selected_ratio=float(count)/float(area)` using **binary32** conversion
and division, then promote the rounded ratios to binary64 for comparisons.
For ordinary profile dimensions, adjustment/state processing is admitted iff
`nz_ratio <= 0.95 || selected_ratio > 0.4`. Otherwise all four history words
and `R` remain unchanged. No image-quality/extraction result is consulted.
For the 9116-pixel profile-9 interior this is exactly `nz<=8660 || count>=3647`.
An all-zero live plane selects all interior pixels, has mean zero, and enters
the low branch; it is not a helper failure. The helper does not reject null
pointers or validate dimensions: those belong to its admitted caller boundary.

Uniform ordinary live values `1..3800` select every interior pixel regardless
of `A`, reference differences, or the first-pass threshold. They have
`m=live_value`, `nz_ratio=selected_ratio=1`; the dynamic result on that domain
has no dependency on the unowned stack word.

An ordinary-pixel illustration of mask sensitivity is uniform reference 2000,
uniform live 3801, current/default DAC 151 and temperature 128. There is no
`d>3800` group. `A=0` gives `C=2424` and selects no interior pixels; support
admission rejects without changing history. `A=0xc000` gives
`C=s32(0xc0000978)` and `P=0xc0002e98`; `s32(P*10)` is negative, so all
interior pixels are selected and the high branch runs. These are evaluations
of the slot-dependent contract, not a specified source of the ambient word.
Both input planes contain legal raw12 values; three uniform-3201 reads remain
independent of this dependency and yield DAC 151, 151, 152 from zero history.

For `A=0`, legal `0..4095` live/reference planes, and `R>294`, the selector
reduces exactly to `L<=3800`. For `L>3800`, `B-L<=294`. If the first-pass high
group exists, its mean is at most 4095 and the low-group mean is at least
-4095 (or zero when empty), so `C>=3800-trunc(8190/4)=1753`. Otherwise
`C=R>294`. Either case makes `B-L>=C` false. This sufficient condition removes
reference dependence from the selected mean and support counts; it does not
apply to arbitrary ambient words or small register values.

## Retained state and complete transition

Four unsigned-16 static words start at zero in the DLL image:

| Address | Symbol used here | Role |
| --- | --- | --- |
| `0x1800615b8` | `active` | adjustment latch |
| `0x1800615bc` | `adjustments` | wrapping adjustment count |
| `0x1800615c0` | `high` | high-mean history |
| `0x1800615c4` | `low` | low-mean history |

`adjustments` is not a step divisor, limit or stop condition. Its increments
wrap at 65536. Reachable `active` is 0/1 and each streak saturates at three.
All predicates below apply only after mask admission.

**High (`m>3200`, unsigned):** if `low<3`, save `active` in a local latch and
clear `low`; otherwise set both the local latch and `active` to zero. Increment
`high` only if below three. If still below three, return. Clear `low`; if the
saved latch is zero, set `active=1` and reset `adjustments=0`. Apply the upward
step below, then increment `adjustments`.

**Low (`m<1500`, unsigned):** exchange `high`/`low` in the preceding transition
and apply the low-branch downward step. Switching from a saturated opposite
streak therefore clears the latch during the first two calls, leaves that
opposite streak saturated until the third, then starts a fresh adjustment run.

**Middle (`1500<=m<=3200`):** if `active==1` and `1900<=m<=2100`, clear
`active`, `high` and `low`, retaining `adjustments` and `R`. If `active==1`
outside that target interval, increment `adjustments` and apply correction:
downward for `m<1900`, upward for `m>2100`. Retain latch and both streaks.
If `active!=1`, independently clear each streak only when it is below three;
saturated streaks survive a middle-valued frame. Leave `adjustments` and `R`
unchanged. The target interval is the unsigned predicate
`u32(m-1900)<=200` at `011a4f..011a5a`.

The target-range exit clears the latch/streaks at `011a5c..011a6c` and
`011b73`, then writes the unchanged register at `011b7b..011b83`. It does not
restore the default DAC. An inactive latch can therefore accompany a previously
adjusted current DAC; current DAC cannot be reconstructed from the four history
words or selected only when the latch is active.

### Exact step and field replacement

For upward adjustment use `delta=u32(m-2000)`; for downward use
`delta=u32(2000-m)`. Zero-extend that delta to 64 bits, convert to binary64,
multiply by `0.25`, and divide by `double(T)*0.283`, in that order. Execute
`CVTTSD2SI` to signed 32 bits (truncation, invalid conversion result
`0x80000000` under the ordinary masked FP environment), take its low 16 bits,
and replace a zero low word with one. Call this unsigned word `step`.

Let `field=R&0x1ff0`. Upward high/correction uses the unsigned comparison
`field + 16*step > 16*(D+30)` in 32 bits. If true choose
`candidate=u16(16*(D+30))`; otherwise `candidate=u16(R+16*step)`.

Low-branch downward uses the **signed 32-bit** comparison
`field < s32(u32(16*(D-4+step)))`. If true choose
`candidate=u16(16*D-64)`; otherwise `candidate=u16(R-16*step)`.
Middle correction downward differs by one in the comparison:
`field < s32(u32(16*(D-5+step)))`, but its capped candidate is still
`u16(16*D-64)`. This is not an interchangeable common clamp.

All branches replace only the nine-bit register field:
`R = R ^ ((candidate ^ R) & 0x1ff0)`; other register bits survive. The wrapper
then publishes `R>>4`. Thus neither the cap nor the low floor is a saturating
clamp over the final whole DAC word. An OTP default near 0 or 511 can cross
the masked field boundary; no additional physical-range clamp is present.

Starting with a nine-bit OTP current DAC, however, repeated wrapper calls keep
current DAC within `0..511`: `007cd0..007cda` constructs a register with no
bits above `0x1fff`, and all three replacement joins (`01192f..011937`,
`011a25..011a2d`, `011b46..011b4e`) alter only `0x1ff0`. The final right shift
preserves that bound. This is an invariant of the OTP-seeded call sequence,
not an extra clamp on the wrapper's general unsigned-16 input contract.

## Acquisition ordering

`0150e0 -> 0055d0 -> 0074bc` builds the four-byte image request with the
**pre-read** DAC. `0074bc` waits for success, copies decoded cache
`0x180060708` to the caller's live buffer, then calls this wrapper on that
cache. A failed send/wait or raw status `-1` returns without adjustment.
Base acquisitions pass adjustment zero; standard live reads pass one and
`&HAL[0x312]`. Publication serializes the **post-read** DAC, even when it
differs from the value used to acquire the image. See
`usbinterface-FUN_180014e10.md` and `usbinterface-FUN_18001fb40.md` for the
count-two, discarded-first-frame, callback and trailer joins.

## Reference and state lifetime

The mask reference is the **latest admitted hardware TX-on base**, not the
reference previously consumed by the algorithm's setup. `015c60` copies the
admitted TX-on plane directly into `HAL+0x248` after both FDT checks and the
image-pair check. It does this even for a direct checkbase caller that does not
set the engine's one-shot refresh marker. Rejection leaves the old reference.
`007c84` obtains that hardware pointer on every enabled call. Current
`device/base.c:goodix_base_ssm_handler` copies the admitted TX-on plane to
`FpiDeviceGoodix53x5.hardware_reference` before releasing or transferring attempt
frames. The independent `milan_generation->setup_tx_on` remains the consumed
setup plane on initialized, unmarked `INVALID_BASE/UP_INVALID_BASE` recovery.

Only `0115a4` directly accesses the four history words. Seed writes are confined
to `007c74`, called by `004a40`; enabled wrapper calls are confined to the
successful `0074bc` path. `0162ac` constructs the profile HAL starting at
`0x1800615f0`, beyond the history words, and `00450c` installs the profile
callbacks and allocates receive buffers without resetting history. History is
DLL-static, not allocated with a request, HAL buffer, image, or engine setup.
Sensor checking reseeds temperature/default and can replace current
`HAL+0x312` from OTP while retaining the previous latch/count/streaks. A retained
initialized D0 route skips sensor checking and keeps both current DAC and
history; see `usbinterface-FUN_180020970.md` for its admission predicate.
`023c40 -> 00e8cc -> HAL+0x1c8/0160a0 -> 00445c` releases HAL/decoded
buffers and event handles without clearing the seed or history words.
Recreating the HAL within the loaded module therefore retains those globals;
fresh module initialization starts their static zero images. There is no
serialized adjustment-history restore at these boundaries. Module unload
scheduling is outside these routines.

The current seed mapping is `device/session.c:GOODIX_OPEN_PARSE_OTP`, which
parses verified OTP into local calibration before
`device/persistence.c:goodix_milan_dac_resume` reconciles current/history.
`calib.dac_h` owns current DAC; `GoodixDynamicDacState.default_dac` retains the
original OTP default. Matching verified identity and seeds preserve newer
same-object RAM. A fresh object can restore current and all four history words
from an identity/seed-bound same-boot sidecar; otherwise it starts at OTP current
and zero history. `goodix53x5.c:goodix_close_joined` calls
`goodix_milan_dac_checkpoint` after transport join to save a changed tuple,
independently of preprocessing/template publication. This Linux lifetime bridge
retains current DAC across its full reinitialization; native full sensor checking
instead reseeds current while native retained D0 preserves it.
The hardware-reference allocation instead ends at joined close, failed open,
full-reinitialization entry, or finalization. It is independent of the generic
`goodix_milan_generation_retain_process` path also used by recoverable rejection.
The current mask uses `A=0` for the native unwritten upper word, retaining `Q=R`.

### Reseeded DAC with retained history

Reseeding current DAC to `D` does not make a retained active latch equivalent
to fresh zero history. On an admitted mean in `2101..3200` or `1500..1899`,
retained `active=1` adjusts immediately; fresh state does not adjust and stays
inactive for an arbitrarily long sequence confined to those middle ranges.
The fresh state needs an extreme high/low trigger to activate. A target-range
mean `1900..2100` clears an active latch and both streaks without adjustment.

With `D=125`, seed temperature 121, and every admitted mean fixed at 2800,
reseeded retained active state yields post-read DACs `126,127,128,...,155`,
then 155; fresh state remains 125. These are fixed-input arithmetic sequences,
not a sensor transfer function. At fixed mean 1800 the corresponding retained
sequence is `124,123,122,121,120,121,120,...`, owing to the distinct middle
downward comparison; fresh state remains 125. Under sustained same-direction
extreme inputs, a retained matching streak of one/two/three first adjusts on
the second/first/first admitted call, versus the third from fresh history.
This bounds that first-trigger delay only, not DAC convergence or elapsed
captures when middle values, support rejection, failures, or reseeds intervene.
