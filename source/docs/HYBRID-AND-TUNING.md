# HybridDeck and tuning menus

These plugins target the ID6 Apps NBT Evo installation. HybridDeck and MG Flasher
use the configured HSFZ gateway. HybridDeck uses connector 117 and MG Flasher
uses 116. They appear under My Vehicle.

## HybridDeck

HybridDeck reads the plug-in hybrid battery unit (SME, address 07) and
standard OBD values from the DME (12). It runs while open, drops expired values,
and pauses after ten minutes so an open screen cannot indefinitely keep the
vehicle awake. Choose **Options → Refresh** to resume.

| Display | Request | Data after the response prefix |
| --- | --- | --- |
| HV charge | SME `22 DDBC` | First unsigned BE16 × 0.1 percent. 0.5–100 accepted. |
| Battery health | SME `22 6335`, then `22 DD7B` | Fourth data byte of `6335` or the single data byte of `DD7B`, as a percent. |
| Pack voltage | SME `22 DDB4`, with `22 DD68` as an alternate | First unsigned BE16 × 0.01 V. |
| Pack current | SME `22 DD69` | First signed BE32 × 0.01 A. |
| Battery coolant | SME `22 DD6C` | First signed BE16 × 0.01 °C. |
| Engine coolant | DME `01 05` | SAE OBD coolant, one unsigned byte minus 40 °C. |
| 12 V module voltage | DME `01 42` | SAE OBD control module voltage. |
| Battery power | Fresh pack voltage and current | −V × A ÷ 1000. Positive means discharge with the eDrive PHEV sign convention. |

These layouts come from the supplied PID index (3-Series hybrid rows) and the
[iDrive7 eDrive battery configuration](https://github.com/cauliflowermossgrape/idrive7-apps/blob/a948390fb8f04ecd68bbac9f5e8620534f35ec25/apps/edrive/app.json#L795-L905).
The upstream project reports DDB4 on the X5 45e. A parked comparison on an ID6
PHEV is still needed. The app renders a dash for missing or out-of-range
readings. State of health is reported by the battery controller. It stays blank when
neither supported reply is available.
[OBDb 3-Series SME definition](https://github.com/OBDb/BMW-3-Series/blob/main/signalsets/v3/default.json)
· [iDrive7 eDrive health definitions](https://github.com/cauliflowermossgrape/idrive7-apps/blob/a948390fb8f04ecd68bbac9f5e8620534f35ec25/apps/edrive/app.json#L646-L666)

## Bootmod3 Suite

The worker clears the existing temporary F301 definition before defining a
marker read. It can check the alternate marker locations from
[EntryNav-Apps](https://github.com/CallMeLewis/EntryNav-Apps/blob/0f588c801fe1902629962215aa8461d3417647ad/src/collector.c#L480-L507).
That lookup automatically selects addresses for map and burble controls from
the bundled address table. Open the map page or choose a map slot or burble
preset directly in iDrive. If the DME refuses the dynamic DID as unsupported,
the worker tries reading the marker directly. Exhaust flap and Active Sound
use fixed requests. Map and burble results still use DME readback.

## MG Flasher

The MG Flasher menu supports **Custom Code 7.3 through 7.3.4**. Open its DME
profile menu and select Gen 1 for A2L prefixes R0/R1 or Gen 2 for R4/F4.
The version and generation come from your tune information. The app requires
a readable setting before sending a command. It checks the confirmed DID and
the feature's new value after acknowledgement. The DME busy response can
trigger a bounded retry. A missing or mismatched readback is shown as
unconfirmed.

Supported controls are map slots, burble presets, Max Cooling, antilag RPM,
exhaust flap, valet mode and ethanol override. Gen 2 also has radiator-flap
control. Refresh reads the available settings, ethanol content, antilag
counter and valet-limit counter. Reports distinguish valid readback from
unavailable data.

This release was tested against a simulated diagnostic gateway. On-car
behavior of this revision is pending.
