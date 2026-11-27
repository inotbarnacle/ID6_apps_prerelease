# Boost packet numeric locale

The shared Boost codec used process-local `snprintf`/`strtod` conversions for
its interprocess packets. A host reproduction against upstream `8567aba`
shows a canonical packet parsing in the C locale and failing in a
comma-decimal locale. The actual head unit's locale has not been captured.

Packets now always use an ASCII period. Encoding retains libc's six-place
rounding and signed zero, then normalizes the decimal separator. Parsing
accepts ASCII numeric tokens and substitutes the current separator in a
bounded local buffer before `strtod`. Decimal exponents and hexadecimal
forms previously accepted in the C locale remain supported. Commas, grouping,
trailing junk, range errors and nonfinite values are rejected. Encoding also
rejects nonfinite values rather than producing a packet the reader rejects.
No signal IDs, units, packet tags, freshness rules or settings change.

The codec reads a bounded copy of `localeconv()->decimal_point`; it does not
call `setlocale` or change other UI formatting. The QNX SDP 6.5 references
document both [localeconv](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_lib_ref/l/localeconv.html)
and [strtod](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_lib_ref/s/strtod.html)
in libc. This avoids depending on newer per-thread locale APIs. As with the
underlying conversions, callers must not change the process locale during a
conversion. A mismatched separator fails validation; this is not a mechanism
for coordinating concurrent global locale changes.

Host checks include a frozen synthetic NBG7 packet produced by the unmodified
baseline, NBG2–7 reads, negative zero, decimal/exponent/hex grammar, malformed
tokens, buffer bounds and 4096 round trips per locale. A missing requested
test locale fails the test. CI builds its comma locale in a temporary
directory without modifying the runner's system configuration. A separate
locale with a two-byte UTF-8 separator checks replacement width. CI runs both
compilers, existing host regressions, static analysis and sanitizers.

These are host results. A licensed QNX build, actual firmware/ABI review and
separately authorized parked runtime validation remain required. No vehicle
commands or installation are part of this change.
