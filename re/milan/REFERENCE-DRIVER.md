# Reference Windows Driver

The Milan notes use the Goodix Fingerprint Sensor Driver for the Dell XPS 13
9305, Dell driver ID **PD5CK**, version **2.0.310.900, A01**, released
**9 November 2022**.

- [Dell driver details](https://www.dell.com/support/home/en-us/drivers/driversdetails?driverid=pd5ck)
- [Direct package download](https://dl.dell.com/FOLDER08940247M/3/Goodix-Fingerprint-Sensor-Driver_PD5CK_WIN64_2.0.310.900_A01_02.EXE)

Package filename:
`Goodix-Fingerprint-Sensor-Driver_PD5CK_WIN64_2.0.310.900_A01_02.EXE`

Dell's published package SHA-256:

```text
e4d2e585367bdc82a5370cea43402ba389b22aebd87b61b778f3e08e1c7ecb21
```

## DLL Identity

The package contains the reference DLLs under
`production/Windows10-x64/0/Drivers/WU/`. They match the installed-driver DLLs
used for this research. Both use image base `0x180000000` in the notes.

| DLL | SHA-256 |
| --- | --- |
| `GoodixEngineAdapter.dll` | `6673db3874fea66a58e2da29e371d797b890c767ba0491134d4a372c5b27e3b4` |
| `usbinterface.dll` | `619f1b708be2d724f4bbe3d08f656586ccb4153afa97a18f659fa8338515e07f` |

These links identify the research source, not a recommendation to install the
package on another laptop. Static analysis only requires extracted files; no
driver installation, firmware write, or PSK replacement is required to read
the documented contracts. Addresses and sensor enums are version-specific.

See [the profile-0 handoff](../../PROFILE0-SUPPORT-SCOPE.md) for the comparison
scope and remaining contributor tasks. Profile 0 remains unsupported by the
current Linux driver.
