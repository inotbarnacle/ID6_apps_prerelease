# ID6 native source

Start with [AgentReadMe.md](../AgentReadMe.md). See [signal definitions](docs/SIGNALS.md) and [porting](docs/PORTING.md).

[Automatic diesel gauges](docs/AUTO-DIESEL-GAUGES.md) uses explicit PID/EGS
evidence for new defaults while retaining existing custom layouts.

[Full-screen diesel telemetry](docs/FULLSCREEN-DIESEL.md) shares the standard
capability policy, preserves legacy layouts and documents the matching bundle,
host previews and pending QNX/vehicle validation.

[Standard exhaust-temperature gauges](docs/STANDARD-EGT.md) use advertised PID
and sensor-support data, retain explicit bank/sensor labels, and preserve old
channel IDs and ordinary packets. Exact N57 support still needs vehicle evidence.

[Optional standard EGR gauges](docs/STANDARD-EGR.md) distinguish requested,
reported actual duty/position and error. They use explicit advertised and
per-field support, retain existing defaults, and do not infer actual values
from error or issue EGR control commands.

[Optional standard pressure gauges](docs/STANDARD-PRESSURE.md) keep requested and
actual A/B values separate, distinguish rail gauge from absolute boost pressure,
and require advertised PID and per-field support. Existing sources and detected
defaults remain unchanged; exact vehicle and QNX validation remain pending.

[ShiftDeck engine profiles](docs/SHIFT-PROFILES.md) covers diesel target/display
settings, legacy preferences, resource generation and host validation limits.

WarmDeck's [independent temperature targets](docs/WARM-READINESS.md) retain fresh
fluid states when another reading is unavailable. Its app targets are advisory;
the display refers drivers to the vehicle's guidance instead of a generic RPM limit.
