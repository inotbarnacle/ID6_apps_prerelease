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
| Coolant temperature | DME `12` | `4300` | Two big-endian bytes, raw ÷ 10 − 273.15 °C. |
| ShiftDeck RPM | DME `12` | OBD `01 0C` | Two big-endian bytes ÷ 4. |
| ShiftDeck speed | DME `12` | OBD `01 0D` | One byte in km/h, displayed in mph. |
| Oil temperature | DME `12` | OBD `01 5C` | One byte, raw − 40 °C. Values above 170 °C are rejected. Read with `--extended`. |
| Battery voltage | DME `12` | OBD `01 42` | Two big-endian bytes ÷ 1000 V (control module voltage). Accepted from 6 to 18 V. Read with `--extended`. |

The two `--extended` channels are standard SAE J1979 PIDs, not BMW DIDs. On an F22 M240i (B58 Gen 1, `NBTevo_S19322I`) at idle the DME answered `01 5C` with `94` (108 °C) and `01 42` with `39 F8` (14.84 V), consistent with the car. On the same car the DME's `01 46` ambient (32 °C against 23 °C on the cluster) and `01 2F` fuel level (78 % against a full gauge) did not match the cluster, so they are not used.

The worker enforces data length, plausibility limits, and freshness. Look at the code for exact bounds and units. If the DME reports coolant via `01 05`, confirm that reading against the intended coolant channel before adding an explicit fallback. An OBD PID and a BMW DID are distinct requests with distinct reply formats. [Request table](../native/src/collector.c) · [Decoder](../native/src/gauge_core.c)
