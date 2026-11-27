#!/bin/sh
set -eu
cd "$(dirname "$0")"
export QNX_HOST="${QNX_HOST:-/opt/qnx650/host/linux/x86}"
export QNX_TARGET="${QNX_TARGET:-/opt/qnx650/target/qnx6}"
export QNX_CONFIGURATION="${QNX_CONFIGURATION:-/etc/qnx}"
frameworks="${NBTEVO_FRAMEWORKS_DIR:-$PWD/frameworks}"
runtime="${NBTEVO_RUNTIME_DIR:-$PWD/runtime}"
for lib in libAppFramework.A.so libCoreFramework.A.so; do
    test -f "$frameworks/$lib" || { echo "Missing private framework: $frameworks/$lib" >&2; exit 1; }
done
export PATH="$QNX_HOST/usr/bin:$PATH"
mkdir -p out
for pair in libm.so:libm.so.2 libsocket.so:libsocket.so.3 libstdc++.so:libstdc++.so.6; do
    name=${pair%:*}
    versioned=${pair#*:}
    if test -f "$runtime/$versioned"; then ln -sf "$runtime/$versioned" "out/$name"; fi
done
ntoarm-gcc -dumpversion
ntoarm-gcc -dumpmachine
c() { qcc -V4.4.2,gcc_ntoarmv7le -Wc,-std=c99 -Wall -Wextra -Werror "$@"; }
cxx() { qcc -V4.4.2,gcc_ntoarmv7le_gpp -lang-c++ -Wc,-std=gnu++98 -Wall -Wextra -Werror "$@"; }
c -Isrc/common -O2 -fPIC -c src/common/gauge_core.c -o out/core.o
c -Isrc/common -O2 -fPIC -c src/common/boost_defaults.c -o out/boost-defaults.o

# Every worker carries the HSFZ-then-MOST link. dlopen and pthreads are in QNX libc; the DSI library is
# opened only at run time, so nothing private is linked.
worker() { c -Isrc/common -Isrc/ipc -Isrc/workers -O2 "$@" src/common/diag_link.c src/common/obd_capabilities.c src/common/most/*.c -Lout -lm -lsocket; }

# Workers
worker src/workers/boost_deck_worker.c src/workers/boost_tone.c out/core.o out/boost-defaults.o -o out/boostdeck-collector-qnx
worker src/workers/mhd_maps_worker.c src/common/gauge_core.c -o out/mhd-maps-qnx
worker src/workers/exhaust_flap_worker.c out/core.o -o out/exhaust-flap-worker-qnx
worker src/workers/shift_deck_worker.c out/core.o -o out/shift-deck-qnx
worker src/workers/engine_codes_worker.c out/core.o -o out/engine-codes-qnx
worker src/workers/bootmod3_suite_worker.c out/core.o -o out/bootmod3-qnx
worker src/workers/mg_flasher_worker.c out/core.o -o out/mgflasher-qnx
worker src/workers/hybrid_deck_worker.c out/core.o -o out/hybriddeck-collector-qnx
worker src/workers/xdrive_control_worker.c out/core.o -o out/xdrive-control-qnx
worker src/workers/beamer_worker.c out/core.o -o out/beamer-qnx
worker src/workers/road_jet_worker.c out/core.o -o out/road-jet-qnx

worker src/workers/warm_deck_worker.c out/core.o -o out/warmdeck-collector-qnx

# The desktop installer stages this static helper on the head unit.
c -O2 -static fileops.c -o out/gaugedeck-fileops

# Plugins
cxx -Isrc/common -Isrc/ipc -Isdk -O2 -fno-strict-aliasing -fno-exceptions -fPIC -shared src/plugins/boost_deck.cpp out/core.o out/boost-defaults.o \
    "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" -Lout -lm -lsocket \
    -Wl,--allow-shlib-undefined -Wl,-soname,libBoostDeck.so -o out/libBoostDeck.so
cxx -Isrc/common -Isrc/ipc -Isdk -O2 -fno-exceptions -fPIC -shared src/plugins/mhd_maps.cpp \
    "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" -Lout \
    -Wl,--allow-shlib-undefined -Wl,-soname,libMhdMaps.so -o out/libMhdMaps.so
cxx -Isrc/common -Isrc/ipc -Isdk -O2 -fno-exceptions -fPIC -shared src/plugins/shift_deck.cpp out/core.o \
    "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" -Lout -lm -lsocket \
    -Wl,--allow-shlib-undefined -Wl,-soname,libShiftDeck.so -o out/libShiftDeck.so
cxx -Isrc/common -Isrc/ipc -Isdk -O2 -fno-strict-aliasing -fno-exceptions -fPIC -shared src/plugins/engine_codes.cpp out/core.o \
    "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" -Lout -lm -lsocket \
    -Wl,--allow-shlib-undefined -Wl,-soname,libEngineCodes.so -o out/libEngineCodes.so
cxx -Isrc/common -Isrc/ipc -Isdk -O2 -fno-exceptions -fPIC -shared src/plugins/bootmod3_suite.cpp \
    "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" -Lout -lsocket \
    -Wl,--allow-shlib-undefined -Wl,-soname,libBootmod3Suite.so -o out/libBootmod3Suite.so
cxx -Isrc/common -Isrc/ipc -Isdk -O2 -fno-exceptions -fPIC -shared src/plugins/xdrive_control.cpp \
    "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" -Lout -lsocket \
    -Wl,--allow-shlib-undefined -Wl,-soname,libXDriveControl.so -o out/libXDriveControl.so
cxx -Isrc/common -Isrc/ipc -Isdk -O2 -fno-exceptions -fPIC -shared src/plugins/beamer.cpp \
    "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" -Lout \
    -Wl,--allow-shlib-undefined -Wl,-soname,libBEAMer.so -o out/libBEAMer.so
cxx -Isrc/common -Isrc/ipc -Isdk -O2 -fno-exceptions -fPIC -shared src/plugins/road_jet.cpp out/core.o \
    "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" -Lout -lm -lsocket \
    -Wl,--allow-shlib-undefined -Wl,-soname,libRoadJet.so -o out/libRoadJet.so
# LaunchDeck reuses out/shift-deck-qnx as its speed worker on its own UDP port.
c -Isrc/common -O2 -fPIC -c src/common/launch_timer.c -o out/launch_timer.o
cxx -Isrc/common -Isrc/ipc -Isdk -O2 -fno-exceptions -fPIC -shared src/plugins/launch_deck.cpp out/core.o out/launch_timer.o \
    "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" -Lout -lm -lsocket \
    -Wl,--allow-shlib-undefined -Wl,-soname,libLaunchDeck.so -o out/libLaunchDeck.so

cxx -Isrc/common -Isrc/ipc -Isdk -O2 -fno-exceptions -fPIC -shared src/plugins/warm_deck.cpp out/core.o \
    "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" -Lout -lm -lsocket \
    -Wl,--allow-shlib-undefined -Wl,-soname,libWarmDeck.so -o out/libWarmDeck.so

cxx -Isrc/common -Isrc/ipc -Isdk -O2 -fno-exceptions -fPIC -shared src/plugins/mg_flasher.cpp \
    "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" -Lout -lsocket \
    -Wl,--allow-shlib-undefined -Wl,-soname,libMgFlasher.so -o out/libMgFlasher.so
cxx -Isrc/common -Isrc/ipc -Isdk -O2 -fno-exceptions -fPIC -shared src/plugins/hybrid_deck.cpp out/core.o \
    "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" -Lout -lsocket \
    -Wl,--allow-shlib-undefined -Wl,-soname,libHybridDeck.so -o out/libHybridDeck.so

# Optional HUD build against the licensed NBT-AAD milestone-7 source.
if test -n "${NBTAAD_SOURCE:-}"; then
    aad="$NBTAAD_SOURCE"
    c -D_QNX_SOURCE -Isrc/common -Isrc/ipc -O2 -c src/workers/parked_check.c -o out/hud-sample.o
    for file in src/common/diag_link.c src/common/most/*.c; do
        c -Isrc/common -O2 -c "$file" -o "out/hud-$(basename "$file" .c).o"
    done
    qcc -V4.4.2,gcc_ntoarmv7le_gpp -lang-c++ -Wc,-std=gnu++0x -O2 -D_QNX_SOURCE \
        -Isrc/common -Isrc/ipc -Isrc/workers -I"$aad" src/workers/hud_shape.cpp "$aad/cluster/hud_terminal.cpp" "$aad/most/client.cpp" "$aad/dsi_client.cpp" \
        out/hud-*.o out/core.o -Lout -lsocket -lm -o out/hud-shape-qnx
    cxx -Isrc/common -Isdk -O2 -fno-exceptions -fPIC -shared src/plugins/hud_menu.cpp \
        "$frameworks/libAppFramework.A.so" "$frameworks/libCoreFramework.A.so" -Lout -lsocket \
        -Wl,--allow-shlib-undefined -Wl,-soname,libHudTest.so -o out/libHudTest.so
fi
