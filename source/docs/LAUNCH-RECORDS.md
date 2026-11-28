# LaunchDeck record read errors

The record loader checks the stream error state immediately after `fread` and
does not request another byte after a failed read or an already reported EOF.
It still probes for trailing data when a full current-format record has been
read without reaching EOF. Invalid or failed reads return an empty record set
and close the stream; the save format and record-update behavior are unchanged.

Both the legacy 10-byte `L1` file (best/last) and current 18-byte `L2` file
(best/last/two prior results) remain readable. Legacy history fields stay zero.
Truncated files, trailing bytes, and implausible values remain rejected.

From `source/`, run `make -C native test-launch-records`. Besides real-file format
tests, Linux/glibc tests use `fopencookie` to return an actual stdio `EIO` before
any bytes and after a plausible legacy prefix. The upstream loader at
`8567abaadd1305965b3ad39e3222575cd49c507d` attempts a second read after each failed
`fread`; the regression fixture detects that extra read. The fixed loader returns
empty records after the first error and closes once.

The glibc error stream is host test infrastructure, not a QNX dependency. The
production change uses standard C stdio only. CI runs the full native suite with
GCC/Clang, the two record tests under sanitizers, and Clang analysis of the
production LaunchDeck translation unit. This does not establish a QNX build,
loader compatibility, or physical head-unit validation.
