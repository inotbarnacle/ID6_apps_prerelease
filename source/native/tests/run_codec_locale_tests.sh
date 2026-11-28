#!/bin/sh
# Linux host checks; keep generated locale data outside system configuration.
set -eu
binary=$1
directory=$2
mkdir -p "$directory"
localedef --no-archive -i de_DE -f UTF-8 "$directory/de_DE.UTF-8"
LOCPATH="$directory" "$binary" de_DE.UTF-8

# U+066B is a two-byte UTF-8 decimal separator. It exercises replacement width.
sed 's/^decimal_point[[:space:]].*/decimal_point "<U066B>"/' \
    /usr/share/i18n/locales/de_DE > "$directory/multibyte.source"
localedef --no-archive -i "$directory/multibyte.source" -f UTF-8 "$directory/de_multibyte.UTF-8"
LOCPATH="$directory" "$binary" de_multibyte.UTF-8
