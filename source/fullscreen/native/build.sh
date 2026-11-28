#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${NBTEVO_FRAMEWORKS_DIR:?Set the directory containing your authorised QNX framework copies}"
: "${NBTEVO_RUNTIME_DIR:?Set the directory containing your authorised QNX runtime copies}"
export QNX_HOST="${QNX_HOST:-/opt/qnx650/host/linux/x86}"
export QNX_TARGET="${QNX_TARGET:-/opt/qnx650/target/qnx6}"
export QNX_CONFIGURATION="${QNX_CONFIGURATION:-/etc/qnx}"
export PATH="$QNX_HOST/usr/bin:$PATH"
mkdir -p native/out/ID6LibraryLab native/out/link
for pair in libstdc++.so:libstdc++.so.6 libsocket.so:libsocket.so.3 libm.so:libm.so.2 libscreen.so:libscreen.so.1; do
    linker="${pair%:*}"
    runtime="${pair#*:}"
    if test -f "$NBTEVO_RUNTIME_DIR/$runtime"; then
        ln -sf "$NBTEVO_RUNTIME_DIR/$runtime" "native/out/link/$linker"
    fi
done
telemetry=native/telemetry
c() { qcc -V4.4.2,gcc_ntoarmv7le -Wc,-std=c99 -include stddef.h -mno-unaligned-access -Wall -Wextra -Werror "$@"; }
c -I"$telemetry/common" -fPIC -O2 -c "$telemetry/common/gauge_core.c" -o native/out/gauge_core.o
build_plugin() {
    name="$1"
    source="$2"
    shift 2
    mkdir -p "native/out/$name"
    qcc -V4.4.2,gcc_ntoarmv7le_gpp -lang-c++ -Wc,-std=gnu++98 -mno-unaligned-access -Wall -Wextra -Werror \
        -D_QNX_SOURCE -Inative/sdk -Inative/generated -I"$telemetry/common" -I"$telemetry/ipc" -O2 -fno-exceptions -fPIC -shared \
        "native/src/$source.cpp" "$@" "$NBTEVO_FRAMEWORKS_DIR/libAppFramework.A.so" \
        "$NBTEVO_FRAMEWORKS_DIR/libCoreFramework.A.so" -Lnative/out/link -lsocket -lm \
        -Wl,--allow-shlib-undefined "-Wl,-soname,lib$name.so" -o "native/out/$name/lib$name.so"
    cp "native/generated/$name.manifest" "native/generated/$name.xml" "native/out/$name/"
}
c -I"$telemetry/common" -fPIC -O2 -c "$telemetry/common/launch_timer.c" -o native/out/launch_timer.o
while read -r name page; do
    build_plugin "$name" library "-DID6_PLUGIN_NAME=\"$name\"" "-DID6_ENTRY_PAGE=$page" \
        native/out/gauge_core.o native/out/launch_timer.o
done < native/generated/modules.txt
if test -n "${ID6_STANDARD_NATIVE_DIR:-}"; then
    . native/build-companions.sh
    build_companions "$ID6_STANDARD_NATIVE_DIR"
fi
worker() {
    output="$1"
    shift
    c -I"$telemetry/common" -I"$telemetry/ipc" -O2 "$@" native/out/gauge_core.o \
        "$telemetry/common/diag_link.c" "$telemetry"/common/most/*.c -Lnative/out/link -lm -lsocket \
        -o "native/out/ID6LibraryLab/$output"
}
worker boostdeck-collector-qnx "$telemetry/workers/boost_deck_worker.c" "$telemetry/workers/boost_tone.c"
worker shift-deck-qnx "$telemetry/workers/shift_deck_worker.c"
worker warmdeck-collector-qnx "$telemetry/workers/warm_deck_worker.c"
worker engine-codes-qnx "$telemetry/workers/engine_codes_worker.c"
worker mhd-maps-qnx "$telemetry/workers/mhd_maps_worker.c"
worker exhaust-flap-worker-qnx "$telemetry/workers/exhaust_flap_worker.c"
worker beamer-qnx "$telemetry/workers/beamer_worker.c"
worker road-jet-qnx "$telemetry/workers/road_jet_worker.c"
cp -R native/generated/layouts native/out/
test -f native/out/link/libscreen.so || { echo "Full-screen build requires the libscreen.so.1 link input" >&2; exit 1; }
c -Inative/sdk -Inative/generated -I"$telemetry/ipc" -I"$telemetry/common" -D_QNX_SOURCE -O2 native/src/fullscreen.c native/src/surface_draw.c native/src/layer_manager.c \
    ${QNX_SCREEN_POPEN_OBJECT:+"$QNX_SCREEN_POPEN_OBJECT"} -Lnative/out/link -lscreen -lsocket -Wl,--allow-shlib-undefined \
    -o native/out/ID6LibraryLab/id6-library-fullscreen-qnx
: "${QNX_OPENSSL_DIR:?Set the directory containing QNX ARM OpenSSL include/ and lib/}"
c -O2 -I"$QNX_OPENSSL_DIR/include" native/usb/app_updates.c \
    "$QNX_OPENSSL_DIR/lib/libcrypto.a" -Lnative/out/link -lsocket \
    -o native/out/ID6LibraryLab/library-usb-update-qnx
cp native/usb/update-key.der native/out/ID6LibraryLab/
