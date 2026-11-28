# Reference signal definitions

These are the reference Gen 1 B58/340i candidates in the current source. They are not a compatibility list for other engines. Requests use HSFZ TCP port 6801 and UDS service `22` unless marked OBD. Positive UDS replies start with `62` plus the requested DID. The table describes the data bytes after that prefix.

| Display | ECU | DID or PID | Reference conversion |
| --- | --- | --- | --- |
| Boost | DME `12` | `4205` and `4201` | Two big-endian bytes each. Absolute kPa is raw ÷ 128, ambient kPa is raw ÷ 256. The collector subtracts ambient and converts to psi. |
| AFR | DME `12` | `5889` | Two bytes. Lambda is raw × 16 ÷ 65535, then converted to gasoline-equivalent AFR. |
| Ignition timing | DME `12` | `4A49` | Two signed big-endian bytes ÷ 10. The reference `FFFF` was ambiguous and stays blank. |
| Intake air | DME `12` | `580F` | One byte, raw × 0.75 − 48 °C. `FF` is rejected. |
| Transmission temperature | EGS `18` | `DA12` | Two big-endian bytes, raw − 48 °C. |
| Rail pressure | DME `12` | `56D7` | Two big-endian bytes, raw ÷ 50 bar. |
| Oil pressure | DME `12` | `586F` | Two big-endian bytes, raw ÷ 1000 bar. |
| Coolant temperature | DME `12` | `4300`, OBD `01 05` fallback | Two big-endian bytes, raw ÷ 10 − 273.15 °C. OBD fallback is one byte minus 40 °C. |
| ShiftDeck RPM | DME `12` | OBD `01 0C` | Two big-endian bytes ÷ 4. |
| ShiftDeck speed | DME `12` | OBD `01 0D` | One byte in km/h, displayed in mph. |
| Oil temperature | DME `12` | OBD `01 5C` | One byte, raw − 40 °C. Values above 170 °C are rejected. Read with `--extended`. |
| Battery voltage | DME `12` | OBD `01 42` | Two big-endian bytes ÷ 1000 V (control module voltage). Accepted from 6 to 18 V. Read with `--extended`. |

The two `--extended` channels are standard SAE J1979 PIDs, not BMW DIDs. On an F22 M240i (B58 Gen 1, `NBTevo_S19322I`) at idle the DME answered `01 5C` with `94` (108 °C) and `01 42` with `39 F8` (14.84 V), consistent with the car. On the same car the DME's `01 46` ambient (32 °C against 23 °C on the cluster) and `01 2F` fuel level (78 % against a full gauge) did not match the cluster, so they are not used.

The worker enforces data length, plausibility limits, and freshness. BoostDeck keeps a fresh BMW coolant reading when available and uses standard OBD coolant when it is unavailable. In [the F87 S55 capture](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/66), the standard PID returned 82 °C and the BMW DID returned one data byte, which the reference decoder rejects. [Request table](../native/src/workers/boost_deck_worker.c) · [Decoder](../native/src/common/gauge_core.c)

## WarmDeck

WarmDeck uses OBD `01 5C` (oil), `01 05` (coolant) and the existing HSFZ EGS
`22 DA12` decoder (transmission). Each reading expires independently. MOST can
supply DME temperatures where supported. The transmission tile requires HSFZ.
WarmDeck timestamps each valid sample when its request starts, so a slow
diagnostic reply consumes part of the freshness window.
If any required reading is unavailable, WarmDeck waits for it before showing a
complete warm-up estimate. The display estimates warm-up from measured trends
and uses 85/75/40 °C as its own targets, with independent per-fluid indicators.
These are app choices, not BMW operating specifications. See
[WarmDeck target behavior](WARM-READINESS.md) for hysteresis, unavailable samples,
estimate limits, and host-only validation.


## BoostDeck selectable OBD readings

Oil temperature (`01 5C`) and module voltage (`01 42`) were checked on the F22 M240i in [PR #102](https://github.com/inotbarnacle/ID6_apps_prerelease/pull/102). The selectable readings also include RPM, speed, calculated load, manifold absolute pressure, air flow, throttle, barometric pressure, outside-air temperature, fuel rate, fuel level, and accelerator pedal. The [decoder](../native/src/common/gauge_core.c) specifies each [SAE J1979](https://doi.org/10.4271/J1979_201202) conversion. Unsupported or stale readings show a dash.


The original nine dashboard positions can also select spark advance (`0E`), absolute load (`43`), short- and long-term fuel trims for banks 1 and 2 (`06`–`09`), commanded lambda (`44`), bank 1 sensor 1 catalyst temperature (`3C`), and commanded throttle (`4C`). These are standardized OBD readings where the ECU reports them. MHD's full logging catalog varies by DME and tune, and the official app also accepts tuner-supplied custom channel definitions. A name from the logger does not establish the request, byte layout, or scale for an ID6 gauge.

The Diesel profile uses `01 05` coolant, `01 0F` intake air, `01 23` direct-injection rail gauge pressure, and `01 0B` manifold pressure minus `01 33` ambient pressure for boost. It retains the existing EGS transmission-temperature query over HSFZ. AFR, timing, oil pressure, and active ethanol require separate DDE-specific evidence. The standard PIDs can also be selected individually where available. A B47 or N57 parked capture is still needed to verify what each DDE supports.


## MHD+ setting display

MHD+ returns a matching echo for accepted map and Max Cooling commands. The menu shows those last accepted choices in the current app session, with **Unknown** until each command is accepted. [MHD documents a Current Map logging channel](https://mhd-tuning.atlassian.net/wiki/spaces/MFS2/pages/11763743/MHD%2BSuite). Its documentation does not publish the request, response layout, or ROM applicability. The [current iDrive7 implementation](https://github.com/cauliflowermossgrape/idrive7-apps/blob/35bca28bc82b30482d73782877ebaf92c69dcb8c/apps/live_adjustments/README.md) checks command echoes and found no changes in its sampled DIDs across a two-slot comparison. The [published map-switch and Max Cooling captures](https://github.com/cauliflowermossgrape/idrive7-apps/pull/23#issuecomment-6520952752) contain the set commands and their replies. They do not include the monitor reading the current setting. [pymhd's Current Map definition](https://github.com/whatotter/pymhd/blob/cad5f80fb6c9526ce161ebff9a37d1bec4266cbe/core/parameters.py#L53) comes from an older I8A0S/N54 logger and does not establish a mapping for the F/G-series DME. A parked capture of the official MHD Monitor while selecting map 1, map 2, then map 1 would identify which field tracks the active slot on that DME. Repeat separately for cooling if MHD exposes it on the vehicle. Changes made from the phone app or steering-wheel controls are invisible to this display. Max Cooling can also reset at engine shutdown. [Upstream MHD command captures](https://github.com/cauliflowermossgrape/idrive7-apps/blob/main/apps/live_adjustments/README.md)

Gasoline rail pressure prefers DME `22 56 D7` and falls back to standard OBD `01 23` when a fresh BMW reading is unavailable. The two-byte OBD value is in 0.1 bar units. The F87 field reports describe a refused BMW request and a working diesel-profile rail gauge. [Issue 66](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/66) · [Issue 118](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/118)

On the Y22292I M140i in [issue #108](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/108), the transmission temperature returned with the same gateway configuration as soon as ENET was connected. The owner measured unresolved ARP on OMAP with ENET unplugged. The MOST fallback reaches the DME, while this transmission reading and [the reported BEAMer setup](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/111) require a reachable HSFZ gateway. The A90 in [issue #79](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/79) has a reachable EGS that refuses the temperature DID over HSFZ.

BoostDeck can continue collecting if another process occupies its optional HUD sharing port. It logs that condition and uses the diagnostic lease for the session. [Issue #122](https://github.com/inotbarnacle/ID6_apps_prerelease/issues/122)
