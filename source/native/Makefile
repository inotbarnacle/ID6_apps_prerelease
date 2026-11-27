CC ?= cc
PYTHON ?= python3
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Werror
CORE = src/common/gauge_core.c
CORE_LINK = $(CORE) src/common/boost_defaults.c src/common/diag_link.c src/common/obd_capabilities.c
HEADERS = src/common/gauge_core.h src/common/boost_signals.def src/ipc/drive_frame.h src/common/diagnostic_lease.h src/common/diag_link.h src/common/gauge_coexistence.h src/common/obd_capabilities.h src/common/boost_defaults.h src/common/obd_egt.h src/common/obd_egr.h src/common/obd_pressure.h
# Every host worker carries the HSFZ-then-MOST link, with a fake DSI selected by ID6_FAKE_MOST.
MOST = src/common/most/most_link.c src/common/most/dsi_client.c src/common/most/most_proto.c src/common/most/dsi_bind.c
MOST_HEADERS = src/common/most/most_link.h src/common/most/dsi_client.h src/common/most/most_proto.h src/common/most/dsi_abi.h
FAKE_DSI = tests/fake_dsi.c tests/fake_dme.c tests/fake_dsi.h tests/fake_dme.h
WORKER_COMMON = $(CORE_LINK) $(MOST) tests/fake_most_env.c $(FAKE_DSI) $(HEADERS) $(MOST_HEADERS)
HOST_LINK = $(CC) $(CFLAGS) -pthread -DMOST_HOST -Isrc/common -Isrc/ipc -Isrc/workers -Itests
.PHONY: assets launch-assets test test-boost-defaults test-egt test-egr test-pressure

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

build/boost-deck: src/workers/boost_deck_worker.c $(WORKER_COMMON) | build
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

build/test-most: tests/test_most.c $(MOST) $(FAKE_DSI) $(MOST_HEADERS) | build
	$(CC) $(CFLAGS) -pthread -DMOST_HOST -Isrc/common -Itests $(filter %.c,$^) -o $@

build/test-diag-link: tests/test_diag_link.c $(CORE_LINK) $(MOST) tests/fake_most_env.c $(FAKE_DSI) $(HEADERS) $(MOST_HEADERS) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/test-launch-timer: tests/test_launch_timer.c src/common/launch_timer.c src/common/launch_timer.h | build
	$(CC) $(CFLAGS) -Isrc/common $(filter %.c,$^) -o $@

build/launch_timer.o: src/common/launch_timer.c src/common/launch_timer.h | build
	$(CC) $(CFLAGS) -Isrc/common -c $< -o $@

build/test-launch-records: tests/test_launch_records.cpp build/launch_timer.o src/common/launch_records.h | build
	$(CXX) -O2 -std=gnu++98 -Wall -Wextra -Werror -Isrc/common $(filter %.cpp %.o,$^) -o $@

build/warmdeck-collector: src/workers/warm_deck_worker.c src/ipc/warm_frame.h $(WORKER_COMMON) | build
	$(HOST_LINK) $(filter %.c,$^) -lm -o $@

build/test-warm-progress: tests/test_warm_progress.cpp src/plugins/warm_progress.h src/ipc/warm_frame.h | build
	$(CXX) -O2 -std=gnu++98 -Wall -Wextra -Werror -Isrc/plugins -Isrc/ipc $< -o $@

build/boost-test-core.o: $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -Isrc/common -c $< -o $@

build/test-boost-preferences: tests/test_boost_preferences.cpp build/boost-test-core.o src/plugins/boost_preferences.h src/common/gauge_preferences.h | build
	$(CXX) -O2 -std=gnu++98 -Wall -Wextra -Werror -Isrc/plugins -Isrc/common $(filter %.cpp %.o,$^) -o $@

launch-assets:
	$(PYTHON) ../tools/launch_art.py

test: test-pressure test-egr test-egt test-boost-defaults build/xdrive-control build/boost-deck build/shift-deck build/mhd-maps build/engine-codes build/test-core build/test-obd-capabilities build/test-most build/test-diag-link build/bootmod3 build/exhaust-flap build/test-launch-timer build/test-launch-records build/test-boost-preferences build/test-warm-progress build/warmdeck-collector build/mgflasher build/hybriddeck-collector
	./build/test-obd-capabilities
	./build/test-warm-progress
	./build/test-boost-preferences
	./build/test-launch-timer
	./build/test-launch-records
	./build/test-core
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
