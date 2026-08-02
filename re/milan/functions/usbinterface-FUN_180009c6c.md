# usbinterface.dll FUN_180009c6c

## Audit Scope

Hardware-module dynamic-configuration loader for `IsOpenAntiFake` and
`HVDacAdjustSwitch`.

## Audit Findings

- Reads optional values from `HKLM\\Software\\Goodix\\FP`.
- Missing values preserve the module's compiled defaults.
- The package defaults are `IsOpenAntiFake=1` and `HVDacAdjustSwitch=0`.
