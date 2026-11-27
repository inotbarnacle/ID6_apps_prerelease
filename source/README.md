# ID6 native source

Start with [AgentReadMe.md](../AgentReadMe.md). See [signal definitions](docs/SIGNALS.md) and [porting](docs/PORTING.md).

[Automatic diesel gauges](docs/AUTO-DIESEL-GAUGES.md) uses explicit PID/EGS
evidence for new defaults while retaining existing custom layouts.

[Standard exhaust-temperature gauges](docs/STANDARD-EGT.md) use advertised PID
and sensor-support data, retain explicit bank/sensor labels, and preserve old
channel IDs and ordinary packets. Exact N57 support still needs vehicle evidence.
