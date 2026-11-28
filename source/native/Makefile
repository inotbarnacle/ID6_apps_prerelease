CC ?= cc
PYTHON ?= python3
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Werror
CORE = src/common/gauge_core.c
CORE_LINK = $(CORE) src/common/diag_link.c
HEADERS = src/common/gauge_core.h src/common/boost_signals.def src/ipc/drive_frame.h src/common/diagnostic_lease.h src/common/diag_link.h src/common/gauge_coexistence.h
# Every host worker carries the HSFZ-then-MOST link, with a fake DSI selected by ID6_FAKE_MOST.
MOST = src/common/most/most_link.c src/common/most/dsi_client.c src/common/most/most_proto.c src/common/most/dsi_bind.c
MOST_HEADERS = src/common/most/most_link.h src/common/most/dsi_client.h src/common/most/most_proto.h src/common/most/dsi_abi.h
FAKE_DSI = tests/fake_dsi.c tests/fake_dme.c tests/fake_dsi.h tests/fake_dme.h
WORKER_COMMON = $(CORE_LINK) $(MOST) tests/fake_most_env.c $(FAKE_DSI) $(HEADERS) $(MOST_HEADERS)
HOST_LINK = $(CC) $(CFLAGS) -pthread -DMOST_HOST -Isrc/common -Isrc/ipc -Isrc/workers -Itests
.PHONY: assets launch-assets test test-warm-readiness test-full-warm test-launch-records

FULLSCREEN_NATIVE ?= ../fullscreen/native
FULL_WARM_INCLUDES = -I$(FULLSCREEN_NATIVE)/src -I$(FULLSCREEN_NATIVE)/generated -isystem $(FULLSCREEN_NATIVE)/sdk -I$(FULLSCREEN_NATIVE)/telemetry/common -I$(FULLSCREEN_NATIVE)/telemetry/ipc

assets: | build
	unzip -p ../../packages/dashboard.zip BoostDeck/BoostDeck_ALL_Images.zip > build/boostdeck-base-images.zip
	$(PYTHON) ../tools/make_boostdeck_extended.py --images build/boostdeck-base-images.zip --images-out build/BoostDeck_ALL_Images.zip
	mv build/BoostDeck_ALL_Images.zip manifests/
	@set -e; for app in MhdMaps ShiftDeck; do \
		name=$${app}_ALL_Images.zip; \
		unzip -p ../../packages/dashboard.zip "$$app/$$name" > "build/$$name"; \
		mv "build/$$name" manifests/; \
	done
	$(PYTHON) ../tools/make_warmdeck.py
	$(PYTHON) ../tools/make_hybrid_and_mg.py

build:
	mkdir -p build

build/boost-deck: src/workers/boost_deck_worker.c src/common/dct_temperature.h $(WORKER_COMMON) | build
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

build/full-warm-core.o: $(FULLSCREEN_NATIVE)/telemetry/common/gauge_core.c $(FULLSCREEN_NATIVE)/telemetry/common/gauge_core.h $(FULLSCREEN_NATIVE)/telemetry/common/boost_signals.def | build
	$(CC) $(CFLAGS) $(FULL_WARM_INCLUDES) -c $< -o $@

build/full-warm-launch.o: $(FULLSCREEN_NATIVE)/telemetry/common/launch_timer.c $(FULLSCREEN_NATIVE)/telemetry/common/launch_timer.h | build
	$(CC) $(CFLAGS) $(FULL_WARM_INCLUDES) -c $< -o $@

build/test-full-warm-plugin: tests/test_full_warm_plugin.cpp tests/fake_bmw.cpp tests/fake_bmw.h $(wildcard $(FULLSCREEN_NATIVE)/src/*.h) $(FULLSCREEN_NATIVE)/src/library.cpp $(wildcard $(FULLSCREEN_NATIVE)/telemetry/common/*.h) src/plugins/warm_progress.h $(FULLSCREEN_NATIVE)/generated/boost_settings_assets.h build/full-warm-core.o build/full-warm-launch.o | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -Wall -Wextra -Werror -fno-access-control -fno-strict-aliasing -DFAKE_BMW_NO_BOOL_MODEL $(FULL_WARM_INCLUDES) -Itests $< tests/fake_bmw.cpp build/full-warm-core.o build/full-warm-launch.o -lm -o $@

build/test-full-warm-surface: tests/test_full_warm_surface.c $(FULLSCREEN_NATIVE)/src/surface_draw.c $(FULLSCREEN_NATIVE)/src/surface_draw.h $(FULLSCREEN_NATIVE)/src/surface_protocol.h $(wildcard $(FULLSCREEN_NATIVE)/generated/*.h) build/full-warm-core.o | build
	$(CC) $(CFLAGS) $(FULL_WARM_INCLUDES) $< build/full-warm-core.o -lm -o $@

build/test-full-surface-session: tests/test_full_surface_session.cpp $(FULLSCREEN_NATIVE)/src/surface_session.h $(FULLSCREEN_NATIVE)/src/surface_protocol.h | build
	$(CXX) $(CXXFLAGS) -std=gnu++98 -Wall -Wextra -Werror -fno-access-control $(FULL_WARM_INCLUDES) $< -o $@

build/full-warm-surface.so: $(FULLSCREEN_NATIVE)/../emulator/host_surface.c $(FULLSCREEN_NATIVE)/src/surface_draw.c $(FULLSCREEN_NATIVE)/telemetry/common/gauge_core.c $(FULLSCREEN_NATIVE)/src/surface_protocol.h $(FULLSCREEN_NATIVE)/src/surface_draw.h $(FULLSCREEN_NATIVE)/src/surface_animation.h $(wildcard $(FULLSCREEN_NATIVE)/generated/*.h) | build
	$(CC) $(CFLAGS) -fPIC -shared $(FULL_WARM_INCLUDES) $(filter %.c,$^) -Wl,--no-undefined -lm -o $@

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

launch-assets:
	$(PYTHON) ../tools/launch_art.py

test: test-warm-readiness build/xdrive-control build/boost-deck build/shift-deck build/mhd-maps build/engine-codes build/test-core build/test-most build/test-diag-link build/bootmod3 build/exhaust-flap build/test-launch-timer build/test-boost-preferences build/test-warm-progress build/warmdeck-collector build/mgflasher build/hybriddeck-collector build/test-core-locale build/beamer build/road-jet test-launch-records
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
