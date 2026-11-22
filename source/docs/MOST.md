# MOST fallback

On some cars the head unit has no route to the HSFZ diagnostic gateway. Without it the workers cannot connect, so the gauges stay blank and map or code requests fail. Every worker can then reach the ECUs through the head unit's own UDS tunnel over MOST.

## Workers

| Worker | DSI client name |
| --- | --- |
| BoostDeck (`boostdeck-collector-qnx`) | `boostdeck` |
| ShiftDeck (`shift-deck-qnx`) | `shiftdeck` |
| MHD Maps (`mhd-maps-qnx`) | `mhdmaps` |
| Engine codes (`engine-codes-qnx`) | `enginecodes` |
| Warm Deck | `warmdeck` |
| Hybrid Deck | `hybriddeck` |
| Beamer | `beamer` |
| MG Flasher | `mgflasher` |
| Bootmod3 | `bootmod3` |
| xDrive Control | `xdrivectl` |
| Road Jet | `roadjet` |
| HUD reader (`hud_reader.c`) | `hudreader` |

All workers use the shared `diag_link` with automatic fallback. Only Bootmod3's burble write is pinned to HSFZ, and `diag_link` keeps every worker on HSFZ when native Android Auto (NBT-AAD) is installed, unless the worker was started with `--transport most`.

## When a worker switches

- A loop worker (BoostDeck, ShiftDeck, Warm Deck, Hybrid Deck, Road Jet, the HUD reader) switches after 5 failed HSFZ connects in a row. A one-shot worker (MHD Maps, engine codes, Bootmod3, xDrive, Beamer, MG Flasher) switches after 1.
- With no `--host`, a worker goes straight to MOST. The plugins always pass the gateway address, so on a car the switch follows failed connects.
- Once HSFZ has connected in a process, that process never switches to MOST.
- A failed MOST open is closed. A loop worker goes back to HSFZ and tries MOST again after 30 s. For a one-shot worker the failure is final.
- Once on MOST, a worker stays there until it is restarted. If the tunnel drops, a loop worker exits and the plugin starts a new one.

The diagnostic workers use automatic fallback by default where supported. The dashboard workers accept a transport override for testing.

## Packaged controls

- **Bootmod3 Suite** can switch maps, request exhaust flap position, and set Active Sound over MOST on a recognized CustomROM. Burble presets require HSFZ because the complete write is larger than the tunnel limit.
- **MHD+ Suite → Options → Exhaust Flap** uses the same DME commands as the previous worker and falls back through MOST when the HSFZ connection fails.
- **xDrive Control** falls back to MOST like the other workers. Its DSC, transfer case and EGS reads and the park check go through the tunnel. The control write (`2E DBE1`) over MOST is untested.

The workers switch transport before sending a control request. A rejected or unconfirmed request is reported to the menu.

## What goes over MOST

- **All read requests can go through the tunnel.** Each request carries the target ECU address, so a UDS read (`22`) is not limited to the DME (`0x12`). The head unit forwards it to the addressed ECU, with no HSFZ route and nothing plugged into OBD.
- Probed read-only on one car on 2026-11-22 with the gauge apps closed: `22 F1 50` was forwarded to the DME (`DME8FF_R`), EGS (`GKEB23TU`) and DSC (`DSC_25`). The transfer case (`19`) refused it: the test car is RWD and has none. With the gauge apps running, the DME read was refused (`noroute`) instead.
- In a test build, `22 DA12` on the EGS returned `00 38` (8 °C with raw − 48) beside DME coolant 7.3 °C on a cold-soaked, engine-off car, and BoostDeck showed the transmission temperature over MOST. This was one cold reading on one car. The scale is not checked warm.
- BoostDeck and Warm Deck now send the EGS reads (`F150` identification, `DA12` transmission temperature) over MOST instead of leaving them blank. Host tests only, not run on a car. Writes and other ECUs' DIDs were not tried.
- Requests use routine `0x0F0B` on FBlock `0x06`, instance `0xF0`, FktID `0x050`, with diagnostic source `0x63`, through `DSYSMOSTServer`.
- A request waits the larger of its own timeout and 2 s. A pending reply extends that up to 5 s in all.
- A reply holds at most 512 bytes, about 127 fault codes. A longer code reply is dropped and shows "No valid code response". A reply that fills all 512 bytes adds "More codes may exist".
- ShiftDeck and the gauges refresh more slowly on MOST. BoostDeck sends each new value as soon as it is read.

## Sharing the tunnel

Workers share the loopback diagnostics lease, as they do on HSFZ. On MOST a loop worker holds it for one request at a time and waits up to 300 ms for it. A one-shot worker holds it for its whole run and waits up to 2 s for it at startup. When a loop worker sees that another worker is waiting, it steps aside before its next request.

## Library and limits

- The DSI library is opened with `dlopen` only when a worker falls back, first by name and then from `/opt/sys/lib`. Nothing links against it. If it or one of its functions is missing, MOST stays off for that process. The plugins add `/opt/sys/lib` to the workers' library path.
- A DSI send has no timeout, so `alarm()` bounds each loop on MOST at 60 s and the stop after SIGTERM at 15 s. On SIGTERM a worker unregisters its target and client before it exits.

## Status

- MOST latency may differ from ENET.
- On our own car (2018 320i, N20, NBT EVO), the same tunnel worked fine in our own custom port for DME `22` reads with nothing plugged into OBD.
- OBD `01`, `2F` (maps, exhaust flap, Max Cooling), `19`/`14` and the session change `10` before a refused clear are untested over MOST.
- Two DSI clients at once are untested on a car. This can happen briefly on page switches.
- This port has not run on a car. On the NBT EVO simulator every worker loaded the DSI library, connected to `DSYSMOSTServer` and got "no target", because the simulator has no tunnel. Two workers opened DSI at the same time there and closed cleanly.

## Host tests

Host builds use a fake DSI library selected by `ID6_FAKE_MOST`:

- unset: no library
- `up`: a DME answering every request the workers send
- `drop`: the link is lost at the fifth request
- `no-server`: the library loads but no server answers
- `mhd-refuse`: `up`, but the map request is refused with NRC `31`
- `codes-512`, `codes-513`: `up`, but the code read returns 127 codes in 512 or 513 bytes

`ID6_FAKE_MOST_DELAY_MS` (up to 2000) slows each reply and the close by that many milliseconds. `make -C native test` runs these.

```sh
boostdeck-collector-qnx --host 169.254.x.y --transport hsfz   # never MOST
boostdeck-collector-qnx --transport most --trace              # MOST only
boostdeck-collector-qnx --host 169.254.x.y                    # auto
```
