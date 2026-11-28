CC ?= cc
PYTHON ?= python3
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Werror
CORE = src/common/gauge_core.c
CORE_LINK = $(CORE) src/common/boost_defaults.c src/common/diag_link.c src/common/obd_capabilities.c
HEADERS = src/common/gauge_core.h src/common/boost_signals.def src/ipc/drive_frame.h src/common/diagnostic_lease.h src/common/diag_link.h src/common/gauge_coexistence.h src/common/obd_capabilities.h src/common/boost_defaults.h src/common/obd_egt.h src/common/obd_egr.h src/common/obd_pressure.h src/common/obd_capabilities_extra.h src/common/obd_reagent.h
# Every host worker carries the HSFZ-then-MOST link, with a fake DSI selected by ID6_FAKE_MOST.
MOST = src/common/most/most_link.c src/common/most/dsi_client.c src/common/most/most_proto.c src/common/most/dsi_bind.c
MOST_HEADERS = src/common/most/most_link.h src/common/most/dsi_client.h src/common/most/most_proto.h src/common/most/dsi_abi.h
FAKE_DSI = tests/fake_dsi.c tests/fake_dme.c tests/fake_dsi.h tests/fake_dme.h
WORKER_COMMON = $(CORE_LINK) $(MOST) tests/fake_most_env.c $(FAKE_DSI) $(HEADERS) $(MOST_HEADERS)
HOST_LINK = $(CC) $(CFLAGS) -pthread -DMOST_HOST -Isrc/common -Isrc/ipc -Isrc/workers -Itests
.PHONY: assets launch-assets test test-build-inputs test-boost-defaults test-egt test-egr test-pressure test-reagent test-launch-records test-shift-profiles test-warm-readiness test-full-warm test-full-shift test-full-boost

FULLSCREEN_NATIVE ?= ../fullscreen/native
FULL_SHARED_HEADERS = $(wildcard src/common/*.h) src/common/boost_signals.def
FULL_WARM_INCLUDES = -I$(FULLSCREEN_NATIVE)/src -I$(FULLSCREEN_NATIVE)/generated -isystem $(FULLSCREEN_NATIVE)/sdk -I$(FULLSCREEN_NATIVE)/telemetry/common -I$(FULLSCREEN_NATIVE)/telemetry/ipc -Isrc/common
FULL_LINK_SOURCES = $(FULLSCREEN_NATIVE)/telemetry/common/diag_link.c $(wildcard $(FULLSCREEN_NATIVE)/telemetry/common/most/*.c) tests/fake_most_env.c $(filter %.c,$(FAKE_DSI))
FULL_LINK_HEADERS = $(wildcard $(FULLSCREEN_NATIVE)/telemetry/common/*.h) $(wildcard $(FULLSCREEN_NATIVE)/telemetry/common/most/*.h) $(FULL_SHARED_HEADERS)

assets: | build
	unzip -p ../../packages/dashboard.zip BoostDeck/BoostDeck_ALL_Images.zip > build/boostdeck-base-images.zip
	$(PYTHON) ../tools/make_boostdeck_extended.py --images build/boostdeck-base-images.zip --images-out build/BoostDeck_ALL_Images.zip
	mv build/BoostDeck_ALL_Images.zip manifests/
	@set -e; for app in MhdMaps ShiftDeck; do \
		name=$${app}_ALL_Images.zip; \
		unzip -p ../../packages/dashboard.zip "$$app/$$name" > "build/$$name"; \
		cp "build/$$name" manifests/; \
	done
	$(PYTHON) ../tools/make_shift_profiles.py --base-images build/ShiftDeck_ALL_Images.zip
	$(PYTHON) ../tools/make_warmdeck.py
	$(PYTHON) ../tools/make_hybrid_and_mg.py

test-build-inputs:
	$(PYTHON) -B tests/test_native_build_inputs.py
	$(PYTHON) -B tests/test_fullscreen_build_inputs.py

build:
	mkdir -p build

build/boost-deck: src/workers/boost_deck_worker.c src/common/dct_temperature.h src/common/obd_capabilities_extra.c $(WORKER_COMMON) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/shift-deck: src/workers/shift_deck_worker.c $(WORKER_COMMON) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/mhd-maps: src/workers/mhd_maps_worker.c $(WORKER_COMMON) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/engine-codes: src/workers/engine_codes_worker.c $(WORKER_COMMON) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/bootmod3: src/workers/bootmod3_suite_worker.c src/workers/bm3_profiles.h $(WORKER_COMMON) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/mgflasher: src/workers/mg_flasher_worker.c $(WORKER_COMMON) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/hybriddeck-collector: src/workers/hybrid_deck_worker.c src/ipc/hybrid_frame.h $(WORKER_COMMON) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/xdrive-control: src/workers/xdrive_control_worker.c $(WORKER_COMMON) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/exhaust-flap: src/workers/exhaust_flap_worker.c $(WORKER_COMMON) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/beamer: src/workers/beamer_worker.c $(WORKER_COMMON) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/road-jet: src/workers/road_jet_worker.c src/ipc/road_jet_frame.h $(WORKER_COMMON) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/test-core: tests/test_core.c $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc/common $(filter %.c,$^) -lm -o $@

build/test-obd-capabilities: tests/test_obd_capabilities.c src/common/obd_capabilities.c $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc/common $(filter %.c,$^) -o $@

build/test-boost-defaults: tests/test_boost_defaults.c src/common/boost_defaults.c $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc/common $(filter %.c,$^) -lm -o $@

build/test-egt: tests/test_egt.c tests/fixtures/boost_legacy_frame.h src/common/boost_defaults.c $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc/common $(filter %.c,$^) -lm -o $@

build/test-egr: tests/test_egr.c tests/fixtures/boost_egt_frame.h $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc/common $(filter %.c,$^) -lm -o $@

build/test-pressure: tests/test_pressure.c tests/fixtures/boost_egr_frame.h $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc/common $(filter %.c,$^) -lm -o $@

build/test-reagent: tests/test_reagent.c tests/fixtures/boost_pressure_frame.h $(CORE) src/common/boost_defaults.c $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc/common $(filter %.c,$^) -lm -o $@

build/test-obd-extra-capabilities: tests/test_obd_extra_capabilities.c src/common/obd_capabilities_extra.c src/common/obd_capabilities.c $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc/common $(filter %.c,$^) -o $@

build/test-reagent-plugin: tests/test_reagent_plugin.cpp tests/fake_bmw.cpp tests/fake_bmw.h build/boost-test-core.o build/boost-defaults.o src/plugins/boost_deck.cpp src/plugins/boost_preferences.h src/plugins/boost_defaults_mode.h $(HEADERS) sdk/bmw_list.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -fno-access-control -Isdk -Isrc/common -Isrc/ipc -Isrc/plugins -Itests $< tests/fake_bmw.cpp build/boost-test-core.o build/boost-defaults.o -lm -o $@

test-reagent: build/test-reagent build/test-obd-extra-capabilities build/test-reagent-plugin build/boost-deck
	./build/test-reagent
	./build/test-obd-extra-capabilities
	./build/test-reagent-plugin
	$(PYTHON) -B tests/test_reagent_transport.py

build/test-pressure-plugin: tests/test_pressure_plugin.cpp tests/fake_bmw.cpp tests/fake_bmw.h build/boost-test-core.o build/boost-defaults.o src/plugins/boost_deck.cpp src/plugins/boost_preferences.h src/plugins/boost_defaults_mode.h $(HEADERS) sdk/bmw_list.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -fno-access-control -Isdk -Isrc/common -Isrc/ipc -Isrc/plugins -Itests $< tests/fake_bmw.cpp build/boost-test-core.o build/boost-defaults.o -lm -o $@

test-pressure: build/test-pressure build/test-pressure-plugin
	./build/test-pressure
	./build/test-pressure-plugin

build/boost-defaults.o: src/common/boost_defaults.c $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc/common -c $< -o $@

build/test-boost-defaults-plugin: tests/test_boost_defaults_plugin.cpp tests/fake_bmw.cpp tests/fake_bmw.h build/boost-test-core.o build/boost-defaults.o src/plugins/boost_deck.cpp src/plugins/boost_preferences.h src/plugins/boost_defaults_mode.h $(HEADERS) sdk/bmw_list.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -fno-access-control -Isdk -Isrc/common -Isrc/ipc -Isrc/plugins -Itests $< tests/fake_bmw.cpp build/boost-test-core.o build/boost-defaults.o -lm -o $@

build/test-egt-plugin: tests/test_egt_plugin.cpp tests/fake_bmw.cpp tests/fake_bmw.h build/boost-test-core.o build/boost-defaults.o src/plugins/boost_deck.cpp src/plugins/boost_preferences.h src/plugins/boost_defaults_mode.h $(HEADERS) sdk/bmw_list.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -fno-access-control -Isdk -Isrc/common -Isrc/ipc -Isrc/plugins -Itests $< tests/fake_bmw.cpp build/boost-test-core.o build/boost-defaults.o -lm -o $@

test-egt: build/test-egt build/test-egt-plugin
	./build/test-egt
	./build/test-egt-plugin

build/test-egr-plugin: tests/test_egr_plugin.cpp tests/fake_bmw.cpp tests/fake_bmw.h build/boost-test-core.o build/boost-defaults.o src/plugins/boost_deck.cpp src/plugins/boost_preferences.h src/plugins/boost_defaults_mode.h $(HEADERS) sdk/bmw_list.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -fno-access-control -Isdk -Isrc/common -Isrc/ipc -Isrc/plugins -Itests $< tests/fake_bmw.cpp build/boost-test-core.o build/boost-defaults.o -lm -o $@

test-egr: build/test-egr build/test-egr-plugin
	./build/test-egr
	./build/test-egr-plugin

test-boost-defaults: build/test-boost-defaults build/test-boost-defaults-plugin
	./build/test-boost-defaults
	./build/test-boost-defaults-plugin

build/test-core-locale: tests/test_core_locale.c tests/fixtures/boost_locale_frame.h $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc/common $(filter %.c,$^) -lm -o $@

build/test-most: tests/test_most.c $(MOST) $(FAKE_DSI) $(MOST_HEADERS) | build
	$(CC) $(CFLAGS) -pthread -DMOST_HOST -Isrc/common -Itests $(filter %.c,$^) -o $@

build/test-diag-link: tests/test_diag_link.c $(CORE_LINK) $(MOST) tests/fake_most_env.c $(FAKE_DSI) $(HEADERS) $(MOST_HEADERS) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/test-launch-timer: tests/test_launch_timer.c src/common/launch_timer.c src/common/launch_timer.h | build
	$(CC) $(CFLAGS) -Isrc/common $(filter %.c,$^) -o $@

build/launch_timer.o: src/common/launch_timer.c src/common/launch_timer.h | build
	$(CC) $(CFLAGS) -Isrc/common -c $< -o $@

build/test-launch-records: tests/test_launch_records.cpp build/launch_timer.o src/common/launch_records.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -Wall -Wextra -Werror -Isrc/common $(filter %.cpp %.o,$^) -o $@

build/test-launch-record-io: tests/test_launch_record_io.cpp build/launch_timer.o src/common/launch_records.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -Wall -Wextra -Werror -Isrc/common $(filter %.cpp %.o,$^) -o $@

test-launch-records: build/test-launch-records build/test-launch-record-io
	./build/test-launch-records
	./build/test-launch-record-io

build/warmdeck-collector: src/workers/warm_deck_worker.c src/common/dct_temperature.h src/ipc/warm_frame.h $(WORKER_COMMON) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/test-warm-progress: tests/test_warm_progress.cpp src/plugins/warm_progress.h src/common/warm_units.h src/common/gauge_preferences.h src/ipc/warm_frame.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -Wall -Wextra -Werror -Isrc/plugins -Isrc/common -Isrc/ipc $< -o $@

build/test-warm-readiness-plugin: tests/test_warm_readiness_plugin.cpp tests/fake_bmw.cpp tests/fake_bmw.h src/plugins/warm_deck.cpp src/plugins/warm_progress.h src/common/warm_units.h src/common/gauge_preferences.h src/ipc/warm_frame.h sdk/bmw_list.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -fno-access-control -Isdk -Isrc/common -Isrc/ipc -Isrc/plugins -Itests $< tests/fake_bmw.cpp -o $@

build/full-warm-core.o: $(FULLSCREEN_NATIVE)/telemetry/common/gauge_core.c $(FULLSCREEN_NATIVE)/telemetry/common/gauge_core.h $(FULLSCREEN_NATIVE)/telemetry/common/boost_signals.def src/common/gauge_core.c $(FULL_SHARED_HEADERS) | build
	$(CC) $(CFLAGS) $(FULL_WARM_INCLUDES) -c $< -o $@

build/full-warm-launch.o: $(FULLSCREEN_NATIVE)/telemetry/common/launch_timer.c $(FULLSCREEN_NATIVE)/telemetry/common/launch_timer.h | build
	$(CC) $(CFLAGS) $(FULL_WARM_INCLUDES) -c $< -o $@

build/test-full-warm-plugin: tests/test_full_warm_plugin.cpp tests/fake_bmw.cpp tests/fake_bmw.h $(wildcard $(FULLSCREEN_NATIVE)/src/*.h) $(FULLSCREEN_NATIVE)/src/library.cpp $(wildcard $(FULLSCREEN_NATIVE)/telemetry/common/*.h) $(FULLSCREEN_NATIVE)/telemetry/ipc/drive_frame.h src/plugins/shift_preferences.h src/plugins/warm_progress.h src/plugins/boost_defaults_mode.h $(FULLSCREEN_NATIVE)/generated/boost_settings_assets.h build/full-warm-core.o build/full-warm-launch.o build/boost-defaults.o | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -Wall -Wextra -Werror -fno-access-control -fno-strict-aliasing -DFAKE_BMW_NO_BOOL_MODEL $(FULL_WARM_INCLUDES) -Itests $< tests/fake_bmw.cpp build/full-warm-core.o build/full-warm-launch.o build/boost-defaults.o -lm -o $@

build/test-full-warm-surface: tests/test_full_warm_surface.c $(FULLSCREEN_NATIVE)/src/surface_draw.c $(FULLSCREEN_NATIVE)/src/surface_draw.h $(FULLSCREEN_NATIVE)/src/surface_shift.h $(FULLSCREEN_NATIVE)/src/surface_protocol.h $(wildcard $(FULLSCREEN_NATIVE)/generated/*.h) build/full-warm-core.o | build
	$(CC) $(CFLAGS) $(FULL_WARM_INCLUDES) $< build/full-warm-core.o -lm -o $@

build/test-full-surface-session: tests/test_full_surface_session.cpp $(FULLSCREEN_NATIVE)/src/surface_session.h $(FULLSCREEN_NATIVE)/src/surface_protocol.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -Wall -Wextra -Werror -fno-access-control $(FULL_WARM_INCLUDES) $< -o $@

build/full-warm-surface.so: $(FULLSCREEN_NATIVE)/../emulator/host_surface.c $(FULLSCREEN_NATIVE)/src/surface_draw.c $(FULLSCREEN_NATIVE)/telemetry/common/gauge_core.c $(FULLSCREEN_NATIVE)/src/surface_protocol.h $(FULLSCREEN_NATIVE)/src/surface_draw.h $(FULLSCREEN_NATIVE)/src/surface_shift.h $(FULLSCREEN_NATIVE)/src/surface_animation.h $(wildcard $(FULLSCREEN_NATIVE)/generated/*.h) src/common/gauge_core.c $(FULL_SHARED_HEADERS) | build
	$(CC) $(CFLAGS) -fPIC -shared $(FULL_WARM_INCLUDES) $(FULLSCREEN_NATIVE)/../emulator/host_surface.c $(FULLSCREEN_NATIVE)/src/surface_draw.c $(FULLSCREEN_NATIVE)/telemetry/common/gauge_core.c -Wl,--no-undefined -lm -o $@

test-full-warm: build/test-full-warm-plugin build/test-full-warm-surface build/test-full-surface-session
	mkdir -p build/full-warm-models
	FULL_WARM_CAPTURE_DIR=build/full-warm-models ./build/test-full-warm-plugin
	./build/test-full-warm-surface
	./build/test-full-surface-session

test-warm-readiness: test-full-warm build/test-warm-progress build/test-warm-readiness-plugin build/warmdeck-collector
	./build/test-warm-progress
	./build/test-warm-readiness-plugin
	$(PYTHON) -B tests/test_warm_freshness.py

build/boost-test-core.o: $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc/common -c $< -o $@

build/test-boost-preferences: tests/test_boost_preferences.cpp build/boost-test-core.o src/plugins/boost_preferences.h src/common/gauge_preferences.h | build
	$(CXX) -O2 -std=gnu++98 -Wall -Wextra -Werror -Isrc/plugins -Isrc/common $(filter %.cpp %.o,$^) -o $@

build/test-shift-preferences: tests/test_shift_preferences.cpp src/plugins/shift_preferences.h src/plugins/shift_assets.h src/common/gauge_preferences.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -Wall -Wextra -Werror -Isrc/plugins -Isrc/common $< -o $@

# Framework fake borrowed from AndreasBoehm's PR #143 (MIT); no factory libraries.
build/test-shift-profile-plugin: tests/test_shift_profile_plugin.cpp tests/fake_bmw.cpp tests/fake_bmw.h src/plugins/shift_deck.cpp src/plugins/shift_preferences.h src/plugins/shift_profile_assets.h src/plugins/shift_assets.h sdk/bmw_list.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -fno-access-control -Isdk -Isrc/common -Isrc/ipc -Isrc/plugins -Itests $< tests/fake_bmw.cpp -o $@

build/test-full-shift-state: tests/test_full_shift_state.cpp $(FULLSCREEN_NATIVE)/src/shift_state.h src/plugins/shift_preferences.h src/plugins/shift_assets.h $(FULLSCREEN_NATIVE)/telemetry/common/gauge_preferences.h $(FULLSCREEN_NATIVE)/telemetry/ipc/drive_frame.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -Wall -Wextra -Werror $(FULL_WARM_INCLUDES) $< -o $@

build/test-full-shift-surface: tests/test_full_shift_surface.c $(FULLSCREEN_NATIVE)/src/surface_draw.c $(FULLSCREEN_NATIVE)/src/surface_draw.h $(FULLSCREEN_NATIVE)/src/surface_shift.h $(FULLSCREEN_NATIVE)/src/surface_protocol.h $(wildcard $(FULLSCREEN_NATIVE)/generated/*.h) build/full-warm-core.o | build
	$(CC) $(CFLAGS) $(FULL_WARM_INCLUDES) $< build/full-warm-core.o -lm -o $@

build/test-full-shift-plugin: tests/test_full_shift_plugin.cpp tests/fake_bmw.cpp tests/fake_bmw.h $(wildcard $(FULLSCREEN_NATIVE)/src/*.h) $(FULLSCREEN_NATIVE)/src/library.cpp $(wildcard $(FULLSCREEN_NATIVE)/telemetry/common/*.h) $(FULLSCREEN_NATIVE)/telemetry/ipc/drive_frame.h src/plugins/shift_preferences.h src/plugins/warm_progress.h src/plugins/boost_defaults_mode.h build/full-warm-core.o build/full-warm-launch.o build/boost-defaults.o | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -Wall -Wextra -Werror -fno-access-control -fno-strict-aliasing -DFAKE_BMW_NO_BOOL_MODEL $(FULL_WARM_INCLUDES) -Itests $< tests/fake_bmw.cpp build/full-warm-core.o build/full-warm-launch.o build/boost-defaults.o -lm -o $@

build/test-full-boost-state: tests/test_full_boost_state.cpp $(FULLSCREEN_NATIVE)/src/boost_state.h $(FULLSCREEN_NATIVE)/src/boost_layout.h $(FULLSCREEN_NATIVE)/src/boost_preferences.h $(FULLSCREEN_NATIVE)/src/fullscreen_preferences.h src/plugins/boost_defaults_mode.h build/full-warm-core.o build/boost-defaults.o | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -Wall -Wextra -Werror $(FULL_WARM_INCLUDES) $< build/full-warm-core.o build/boost-defaults.o -lm -o $@

build/test-full-boost-plugin: tests/test_full_boost_plugin.cpp tests/fake_bmw.cpp tests/fake_bmw.h $(wildcard $(FULLSCREEN_NATIVE)/src/*.h) $(FULLSCREEN_NATIVE)/src/library.cpp $(wildcard $(FULLSCREEN_NATIVE)/generated/*.h) $(FULL_SHARED_HEADERS) src/plugins/boost_defaults_mode.h build/full-warm-core.o build/full-warm-launch.o build/boost-defaults.o | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -Wall -Wextra -Werror -fno-access-control -fno-strict-aliasing -DFAKE_BMW_NO_BOOL_MODEL $(FULL_WARM_INCLUDES) -Itests $< tests/fake_bmw.cpp build/full-warm-core.o build/full-warm-launch.o build/boost-defaults.o -lm -o $@

build/test-full-boost-surface: tests/test_full_boost_surface.c $(FULLSCREEN_NATIVE)/src/surface_draw.c $(wildcard $(FULLSCREEN_NATIVE)/src/*.h) $(wildcard $(FULLSCREEN_NATIVE)/generated/*.h) $(FULL_SHARED_HEADERS) build/full-warm-core.o | build
	$(CC) $(CFLAGS) $(FULL_WARM_INCLUDES) $< build/full-warm-core.o -lm -o $@

test-full-boost: build/test-full-boost-state build/test-full-boost-plugin build/test-full-boost-surface build/full-boost-deck build/full-warm-collector
	./build/test-full-boost-state
	mkdir -p build/full-boost-models
	FULL_BOOST_CAPTURE_DIR=build/full-boost-models ./build/test-full-boost-plugin
	./build/test-full-boost-surface
	$(PYTHON) -B tests/test_full_boost_worker.py

build/full-boost-deck: $(FULLSCREEN_NATIVE)/telemetry/workers/boost_deck_worker.c src/workers/boost_deck_worker.c $(FULL_LINK_SOURCES) $(FULL_LINK_HEADERS) src/common/obd_capabilities.c src/common/obd_capabilities_extra.c build/full-warm-core.o build/boost-defaults.o | build
	$(CC) $(CFLAGS) -pthread -DMOST_HOST $(FULL_WARM_INCLUDES) -Itests $(FULLSCREEN_NATIVE)/telemetry/workers/boost_deck_worker.c $(FULL_LINK_SOURCES) src/common/obd_capabilities.c src/common/obd_capabilities_extra.c build/full-warm-core.o build/boost-defaults.o -lm -o $@

build/full-warm-collector: $(FULLSCREEN_NATIVE)/telemetry/workers/warm_deck_worker.c src/workers/warm_deck_worker.c $(FULL_LINK_SOURCES) $(FULL_LINK_HEADERS) src/common/obd_capabilities.c $(FULLSCREEN_NATIVE)/telemetry/ipc/warm_frame.h build/full-warm-core.o | build
	$(CC) $(CFLAGS) -pthread -DMOST_HOST $(FULL_WARM_INCLUDES) -Itests $(FULLSCREEN_NATIVE)/telemetry/workers/warm_deck_worker.c $(FULL_LINK_SOURCES) src/common/obd_capabilities.c build/full-warm-core.o -lm -o $@

build/full-shift-deck: $(FULLSCREEN_NATIVE)/telemetry/workers/shift_deck_worker.c $(FULLSCREEN_NATIVE)/telemetry/common/diag_link.c $(wildcard $(FULLSCREEN_NATIVE)/telemetry/common/most/*.c) tests/fake_most_env.c $(FAKE_DSI) $(wildcard $(FULLSCREEN_NATIVE)/telemetry/common/*.h) $(wildcard $(FULLSCREEN_NATIVE)/telemetry/common/most/*.h) $(FULLSCREEN_NATIVE)/telemetry/ipc/drive_frame.h build/full-warm-core.o | build
	$(CC) $(CFLAGS) -pthread -DMOST_HOST $(FULL_WARM_INCLUDES) -Itests $(filter %.c %.o,$^) -lm -o $@

test-full-shift: build/test-full-shift-state build/test-full-shift-surface build/test-full-shift-plugin build/full-shift-deck
	./build/test-full-shift-state
	./build/test-full-shift-surface
	mkdir -p build/full-shift-models
	FULL_SHIFT_CAPTURE_DIR=build/full-shift-models ./build/test-full-shift-plugin
	$(PYTHON) -B tests/test_full_shift_worker.py

test-shift-profiles: test-full-shift build/test-shift-preferences build/test-shift-profile-plugin
	./build/test-shift-preferences
	mkdir -p build/shift-preview-models
	./build/test-shift-profile-plugin build/shift-preview-models

launch-assets:
	$(PYTHON) ../tools/launch_art.py

test: test-build-inputs test-full-boost test-reagent test-pressure test-egr test-egt test-boost-defaults build/xdrive-control build/boost-deck build/shift-deck build/mhd-maps build/engine-codes build/test-core build/test-obd-capabilities build/test-most build/test-diag-link build/bootmod3 build/exhaust-flap build/test-launch-timer build/test-boost-preferences build/test-warm-progress build/warmdeck-collector build/mgflasher build/hybriddeck-collector build/test-core-locale build/beamer build/road-jet test-launch-records test-shift-profiles test-warm-readiness
	./build/test-obd-capabilities
	./build/test-boost-preferences
	./build/test-launch-timer
	./build/test-core
	./build/test-core-locale
	./build/test-most
	./build/test-diag-link
	$(PYTHON) -B tests/test_udp_ports.py
	$(PYTHON) -B tests/test_drive_apps.py
	$(PYTHON) -B tests/test_oneshot_most.py
	$(PYTHON) -B tests/test_transport.py
	$(PYTHON) -B tests/test_diesel_capabilities.py
	$(PYTHON) -B tests/test_egt_transport.py
	$(PYTHON) -B tests/test_egr_transport.py
	$(PYTHON) -B tests/test_pressure_transport.py
