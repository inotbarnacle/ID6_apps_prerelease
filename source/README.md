# ID6 native source

Start with [AgentReadMe.md](../AgentReadMe.md). See [signal definitions](docs/SIGNALS.md) and [porting](docs/PORTING.md).

[Engine fault-code handling](docs/FAULT-CODES.md) describes supported status bits,
the DDE/raw view, separate standard confirmed/pending/permanent emissions-code
reads, description limitations and explicit clear confirmation. Standard OBD
codes are parsed from their own response format, never shortened BMW UDS codes.
