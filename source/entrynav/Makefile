# EntryNav-Apps: BoostDeck, ShiftDeck, Bootmod3 Suite, MHD+ Suite and Engine Fault Codes for the EntryNav2 (ARMv7
# hard-float Linux, glibc 2.15).
#
#   make test        host tests: the decoders and the workers against a simulated gateway (cc, python3)
#   make assets      the screens' XML and image archives (python3 with Pillow)
#   bash toolchain/tc.sh make arm
#                    the ARM plugins, workers and test programs, in the build image against the copy of the car
#   bash toolchain/qemu.sh python3 tests/run_arm.py
#                    the ARM checks: each plugin loaded with the car's own HMI libraries under QEMU, and its
#                    vtable compared slot by slot with the car's bmw::PluginApp
#   make clean       every build output

CC ?= cc
CXX ?= g++
PYTHON ?= python3
SHELLCHECK ?= shellcheck
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Werror
CORE = src/gauge_core.c
HEADERS = src/gauge_core.h src/drive_frame.h src/diagnostic_lease.h
BM3 = src/bm3.c src/bm3.h src/bm3_profiles.h
.PHONY: assets check syntax lint generated test arm clean

assets:
	$(PYTHON) tools/make_bars.py
	$(PYTHON) tools/make_drive_apps.py

# Every check that needs no copy of the car's system. CI (.github/workflows/ci.yml) runs each as its own job.
check: test syntax lint generated

# The apps' C++ on the PC, for errors only: the ARM build needs the car's libraries.
syntax:
	for f in src/*.cpp; do $(CXX) -std=gnu++98 -Wall -Wextra -Werror -fsyntax-only -Isrc -Isdk "$$f" || exit 1; done

lint:
	$(SHELLCHECK) -s sh -S style install/setup.sh
	$(SHELLCHECK) -S style toolchain/tc.sh toolchain/qemu.sh

# The committed screens and headers are what the generators make now.
generated: assets
	git diff --exit-code -- plugin/*.xml plugin/*.manifest src/dial_scales.h src/shift_assets.h

# ---- host ------------------------------------------------------------------------------------------------------------
build:
	mkdir -p build

build/boostdeck-collector: src/collector.c $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc $(filter %.c,$^) -lm -o $@

build/shift-deck: src/collector.c $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc -DSHIFT_DECK_ONLY $(filter %.c,$^) -lm -o $@

build/bm3-maps: src/collector.c $(CORE) $(HEADERS) $(BM3) src/paths.h | build
	$(CC) $(CFLAGS) -Isrc -DBM3_MAPS_ONLY $(filter %.c,$^) -lm -o $@

build/mhd-maps: src/collector.c $(CORE) $(HEADERS) src/paths.h | build
	$(CC) $(CFLAGS) -Isrc -DMHD_MAPS_ONLY $(filter %.c,$^) -lm -o $@

build/ena-gateway: src/collector.c $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc -DGATEWAY_TOOL $(filter %.c,$^) -lm -o $@

build/engine-codes: src/collector.c $(CORE) $(HEADERS) src/paths.h | build
	$(CC) $(CFLAGS) -Isrc -DENGINE_CODES_ONLY $(filter %.c,$^) -lm -o $@

build/test-core: tests/test_core.c $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc $(filter %.c,$^) -lm -o $@

build/test-bm3: tests/test_bm3.c $(BM3) | build
	$(CC) $(CFLAGS) -Isrc $(filter %.c,$^) -o $@

test: build/boostdeck-collector build/shift-deck build/bm3-maps build/mhd-maps build/ena-gateway build/engine-codes \
      build/test-core build/test-bm3
	./build/test-core
	./build/test-bm3
	$(PYTHON) -B tests/test_transport.py
	$(PYTHON) -B tests/test_drive_apps.py
	$(PYTHON) -B tests/test_bm3_worker.py
	$(PYTHON) -B tests/test_mhd_worker.py
	$(PYTHON) -B tests/test_engine_codes.py
	$(PYTHON) -B tests/test_gateway_tool.py
	$(PYTHON) -B tests/test_probe_signals.py

# ---- ARM (run through toolchain/tc.sh) --------------------------------------------------------------------------------
ARM_CC  = arm-linux-gnueabihf-gcc
ARM_CXX = arm-linux-gnueabihf-g++
SYSROOT ?= /sysroot
ARCH = -march=armv7-a -mfpu=neon -mfloat-abi=hard
ARM_CFLAGS = -O2 -std=c99 -Wall -Wextra -Werror $(ARCH)
# The plugins: C++98 like the NBT Evo originals, no exceptions, hidden symbols except pluginAppInfo.
ARM_CXXFLAGS = -O2 -std=gnu++98 -Wall -Wextra -Werror $(ARCH) -fPIC -fno-exceptions -fno-strict-aliasing \
               -fvisibility=hidden -fvisibility-inlines-hidden
LIBDIRS = -L$(SYSROOT)/usr/lib -L$(SYSROOT)/lib -Wl,-rpath-link,$(SYSROOT)/usr/lib:$(SYSROOT)/lib
HMI_LIBS = -L$(SYSROOT)/opt/hmi/lib -Wl,-rpath-link,$(SYSROOT)/opt/hmi/lib -l:libAppFramework.A.so -l:libCoreFramework.A.so
PLUGIN_LINK = -shared -Wl,--allow-shlib-undefined -Wl,-z,defs $(LIBDIRS) $(HMI_LIBS) -lrt

build-arm/core.o: $(CORE) $(HEADERS) | build-arm
	$(ARM_CC) $(ARM_CFLAGS) -fPIC -fvisibility=hidden -Isrc -c $(CORE) -o $@

build-arm:
	mkdir -p build-arm

build-arm/libBoostDeck.so: src/boost_deck.cpp build-arm/core.o src/*.h sdk/bmw_list.h | build-arm
	$(ARM_CXX) $(ARM_CXXFLAGS) -Isrc -Isdk src/boost_deck.cpp build-arm/core.o -o $@ -Wl,-soname,libBoostDeck.so \
	    $(PLUGIN_LINK) -lm
build-arm/libShiftDeck.so: src/shift_deck.cpp build-arm/core.o src/*.h sdk/bmw_list.h | build-arm
	$(ARM_CXX) $(ARM_CXXFLAGS) -Isrc -Isdk src/shift_deck.cpp build-arm/core.o -o $@ -Wl,-soname,libShiftDeck.so \
	    $(PLUGIN_LINK) -lm
build-arm/libBm3Maps.so: src/bm3_maps.cpp src/*.h sdk/bmw_list.h | build-arm
	$(ARM_CXX) $(ARM_CXXFLAGS) -Isrc -Isdk src/bm3_maps.cpp -o $@ -Wl,-soname,libBm3Maps.so $(PLUGIN_LINK)
build-arm/libMhdMaps.so: src/mhd_maps.cpp src/*.h sdk/bmw_list.h | build-arm
	$(ARM_CXX) $(ARM_CXXFLAGS) -Isrc -Isdk src/mhd_maps.cpp -o $@ -Wl,-soname,libMhdMaps.so $(PLUGIN_LINK)
build-arm/libEngineCodes.so: src/engine_codes.cpp src/*.h sdk/bmw_list.h | build-arm
	$(ARM_CXX) $(ARM_CXXFLAGS) -Isrc -Isdk src/engine_codes.cpp -o $@ -Wl,-soname,libEngineCodes.so $(PLUGIN_LINK)

build-arm/boostdeck-collector: src/collector.c $(CORE) $(HEADERS) | build-arm
	$(ARM_CC) $(ARM_CFLAGS) -Isrc src/collector.c $(CORE) -o $@ $(LIBDIRS) -lm -lrt
build-arm/shift-deck: src/collector.c $(CORE) $(HEADERS) | build-arm
	$(ARM_CC) $(ARM_CFLAGS) -Isrc -DSHIFT_DECK_ONLY src/collector.c $(CORE) -o $@ $(LIBDIRS) -lm -lrt
build-arm/bm3-maps: src/collector.c $(CORE) $(HEADERS) $(BM3) src/paths.h | build-arm
	$(ARM_CC) $(ARM_CFLAGS) -Isrc -DBM3_MAPS_ONLY src/collector.c src/bm3.c $(CORE) -o $@ $(LIBDIRS) -lm -lrt
build-arm/mhd-maps: src/collector.c $(CORE) $(HEADERS) src/paths.h | build-arm
	$(ARM_CC) $(ARM_CFLAGS) -Isrc -DMHD_MAPS_ONLY src/collector.c $(CORE) -o $@ $(LIBDIRS) -lm -lrt
build-arm/ena-gateway: src/collector.c $(CORE) $(HEADERS) | build-arm
	$(ARM_CC) $(ARM_CFLAGS) -Isrc -DGATEWAY_TOOL src/collector.c $(CORE) -o $@ $(LIBDIRS) -lm -lrt
build-arm/engine-codes: src/collector.c $(CORE) $(HEADERS) src/paths.h | build-arm
	$(ARM_CC) $(ARM_CFLAGS) -Isrc -DENGINE_CODES_ONLY src/collector.c $(CORE) -o $@ $(LIBDIRS) -lm -lrt

build-arm/test-plugin-load: tests/test_plugin_load.cpp | build-arm
	$(ARM_CXX) -O2 -Wall -Wextra -Werror $(ARCH) tests/test_plugin_load.cpp -o $@ $(LIBDIRS) -ldl
build-arm/test-spawn: tests/test_spawn.cpp src/diagnostic_worker.h src/paths.h | build-arm
	$(ARM_CXX) -O2 -Wall -Wextra -Werror $(ARCH) -Isrc tests/test_spawn.cpp -o $@ $(LIBDIRS)
build-arm/test-bm3: tests/test_bm3.c $(BM3) | build-arm
	$(ARM_CC) $(ARM_CFLAGS) -Isrc tests/test_bm3.c src/bm3.c -o $@ $(LIBDIRS)

arm: build-arm/libBoostDeck.so build-arm/libShiftDeck.so build-arm/libBm3Maps.so build-arm/libMhdMaps.so \
     build-arm/libEngineCodes.so build-arm/boostdeck-collector build-arm/shift-deck build-arm/bm3-maps build-arm/mhd-maps \
     build-arm/ena-gateway build-arm/engine-codes build-arm/test-plugin-load build-arm/test-spawn build-arm/test-bm3

clean:
	rm -rf build build-arm


# Standalone RHMI ports of the remaining ID6Apps.
PORT_DIR = src/ports
PORT_DEPS = $(wildcard $(PORT_DIR)/*.h $(PORT_DIR)/plugins/*.h)
PORT_CORE = $(PORT_DIR)/gauge_core.c
PORT_LINK = $(PORT_DIR)/diag_link.c
PORT_INCLUDES = -I$(PORT_DIR)/plugins -I$(PORT_DIR) -Isrc -Isdk
PORT_RESOURCES = $(wildcard $(PORT_DIR)/plugins/*.inc)

build-arm/port-core.o: $(PORT_CORE) $(PORT_DEPS) | build-arm
	$(ARM_CC) $(ARM_CFLAGS) -fPIC -fvisibility=hidden -I$(PORT_DIR) -c $< -o $@

build-arm/port-timer.o: $(PORT_DIR)/launch_timer.c $(PORT_DEPS) | build-arm
	$(ARM_CC) $(ARM_CFLAGS) -fPIC -fvisibility=hidden -I$(PORT_DIR) -c $< -o $@

define port-plugin
build-arm/lib$(1).so: $(PORT_DIR)/plugins/$(2).cpp build-arm/port-core.o $(3) $(PORT_DEPS) $(PORT_RESOURCES) | build-arm
	$$(ARM_CXX) $$(ARM_CXXFLAGS) $$(PORT_INCLUDES) $$< build-arm/port-core.o $(3) -Wl,-soname,lib$(1).so $$(PLUGIN_LINK) -lm -o $$@
endef

define port-worker
build-arm/$(1): $(PORT_DIR)/workers/$(2) $(PORT_CORE) $(PORT_LINK) $(PORT_DEPS) | build-arm
	$$(ARM_CC) $$(ARM_CFLAGS) -I$$(PORT_DIR) $$(filter %.c,$$^) $$(LIBDIRS) -lm -lrt -o $$@

build/$(1): $(PORT_DIR)/workers/$(2) $(PORT_CORE) $(PORT_LINK) $(PORT_DEPS) | build
	$$(CC) $$(CFLAGS) -I$$(PORT_DIR) $$(filter %.c,$$^) -lm -o $$@
endef

$(eval $(call port-plugin,WarmDeck,warm_deck,))
$(eval $(call port-plugin,LaunchDeck,launch_deck,build-arm/port-timer.o))
$(eval $(call port-plugin,HybridDeck,hybrid_deck,))
$(eval $(call port-plugin,RoadJet,road_jet,))
$(eval $(call port-plugin,BEAMer,beamer,))
$(eval $(call port-plugin,XDriveControl,xdrive_control,))
$(eval $(call port-plugin,MgFlasher,mg_flasher,))
$(eval $(call port-worker,warmdeck-collector,warm_deck_worker.c))
$(eval $(call port-worker,launch-drive-worker,shift_deck_worker.c))
$(eval $(call port-worker,hybriddeck-collector,hybrid_deck_worker.c))
$(eval $(call port-worker,road-jet,road_jet_worker.c))
$(eval $(call port-worker,beamer,beamer_worker.c))
$(eval $(call port-worker,xdrive-control,xdrive_control_worker.c))
$(eval $(call port-worker,mgflasher,mg_flasher_worker.c))

arm: build-arm/libWarmDeck.so build-arm/libLaunchDeck.so build-arm/libHybridDeck.so build-arm/libRoadJet.so build-arm/libBEAMer.so build-arm/libXDriveControl.so build-arm/libMgFlasher.so build-arm/warmdeck-collector build-arm/launch-drive-worker build-arm/hybriddeck-collector build-arm/road-jet build-arm/beamer build-arm/xdrive-control build-arm/mgflasher
port-workers: build/warmdeck-collector build/launch-drive-worker build/hybriddeck-collector build/road-jet build/beamer build/xdrive-control build/mgflasher
port-syntax:
	for f in $(PORT_DIR)/plugins/*.cpp; do $(CXX) -std=gnu++98 -Wall -Wextra -Werror -fsyntax-only $(PORT_INCLUDES) "$$f" || exit 1; done
