CC ?= cc
PYTHON ?= python3
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Werror
CORE = src/common/gauge_core.c
CORE_LINK = $(CORE) src/common/diag_link.c
HEADERS = src/common/gauge_core.h src/common/boost_signals.def src/ipc/drive_frame.h src/common/diagnostic_lease.h src/common/diag_link.h src/common/gauge_coexistence.h src/common/f150_table.h src/common/dme_signal_table.h src/common/dsc_control_table.h src/common/egs_signal_table.h
# Every host worker carries the HSFZ-then-MOST link, with a fake DSI selected by ID6_FAKE_MOST.
MOST = src/common/most/most_link.c src/common/most/dsi_client.c src/common/most/most_proto.c src/common/most/dsi_bind.c
MOST_HEADERS = src/common/most/most_link.h src/common/most/dsi_client.h src/common/most/most_proto.h src/common/most/dsi_abi.h
FAKE_DSI = tests/fake_dsi.c tests/fake_dme.c tests/fake_dsi.h tests/fake_dme.h
WORKER_COMMON = $(CORE_LINK) $(MOST) tests/fake_most_env.c $(FAKE_DSI) $(HEADERS) $(MOST_HEADERS)
HOST_LINK = $(CC) $(CFLAGS) -pthread -DMOST_HOST -Isrc/common -Isrc/ipc -Isrc/workers -Itests
.PHONY: assets launch-assets test test-plugins check-poll-coverage check-reap

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

build/test-f150: tests/test_f150.c src/common/f150_table.h src/common/dme_signal_table.h src/common/egs_signal_table.h | build
	$(CC) $(CFLAGS) -Isrc/common $(filter %.c,$^) -o $@

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

# Plugin logic on the host, against a fake BMW framework (tests/fake_bmw.cpp). The display transcripts
# in tests/golden/ come from the plugins before the polling and refresh changes: they must not change.
PLUGIN_HOST = $(CXX) -std=gnu++98 -O1 -w -fno-access-control -Isdk -Isrc/common -Isrc/ipc -Isrc/plugins -Itests
PLUGIN_FAKE = tests/fake_bmw.cpp tests/fake_bmw.h build/gauge_core.o build/launch_timer.o

build/gauge_core.o: src/common/gauge_core.c src/common/gauge_core.h | build
	$(CC) -O2 -std=c99 -Isrc/common -c $< -o $@

build/test-boost-deck: tests/test_boost_deck_host.cpp src/plugins/boost_deck.cpp $(PLUGIN_FAKE) | build
	$(PLUGIN_HOST) -DBOOST_DECK_SOURCE='"$(abspath src/plugins/boost_deck.cpp)"' $< tests/fake_bmw.cpp build/gauge_core.o -lm -o $@

build/test-shift-deck: tests/test_drive_deck_host.cpp src/plugins/shift_deck.cpp $(PLUGIN_FAKE) | build
	$(PLUGIN_HOST) -DDECK_SOURCE='"$(abspath src/plugins/shift_deck.cpp)"' -DDECK_CLASS=ShiftDeck -DDECK_PORT=42863 $< tests/fake_bmw.cpp build/gauge_core.o -lm -o $@

build/test-launch-deck: tests/test_drive_deck_host.cpp src/plugins/launch_deck.cpp $(PLUGIN_FAKE) | build
	$(PLUGIN_HOST) -DDECK_SOURCE='"$(abspath src/plugins/launch_deck.cpp)"' -DDECK_CLASS=LaunchDeck -DDECK_PORT=42864 -DDECK_LAUNCH $< tests/fake_bmw.cpp build/gauge_core.o build/launch_timer.o -lm -o $@

# A plugin whose poll timer follows the page (stops while hidden, restarts on show) needs a hidden-polling test.
# boost, shift and launch have their own above; the rest share tests/test_poll_lifecycle_host.cpp, listed here as
# file:Class. check-poll-coverage fails for any plugin that posts a poll message and tracks visibility but is
# in neither list, so a new plugin cannot skip this. Goldens come from main: tests/golden/<file>-display.txt.
POLL_LIFECYCLE = warm_deck:WarmDeck hybrid_deck:HybridDeck road_jet:RoadJet
POLL_NAMES = $(foreach p,$(POLL_LIFECYCLE),$(word 1,$(subst :, ,$p)))
POLL_EXEMPT = beamer  # polls only while its worker runs, whatever the page does

define POLL_RULE
build/test-$(1): tests/test_poll_lifecycle_host.cpp src/plugins/$(1).cpp $$(PLUGIN_FAKE) | build
	$$(PLUGIN_HOST) -DPLUGIN_SOURCE='"$$(abspath src/plugins/$(1).cpp)"' -DPLUGIN_CLASS=$(2) $$< tests/fake_bmw.cpp build/gauge_core.o -lm -o $$@
test-plugins-$(1): build/test-$(1)
	./build/test-$(1) display | diff - tests/golden/$(1)-display.txt
	./build/test-$(1) polling
endef
$(foreach p,$(POLL_LIFECYCLE),$(eval $(call POLL_RULE,$(word 1,$(subst :, ,$p)),$(word 2,$(subst :, ,$p)))))

check-poll-coverage:
	@for f in $$(grep -l 0x40000003u src/plugins/*.cpp | xargs grep -l 'visible_'); do b=$$(basename $$f .cpp); \
	  case " boost_deck shift_deck launch_deck $(POLL_NAMES) $(POLL_EXEMPT) " in *" $$b "*) ;; \
	  *) echo "$$b polls on a timer but has no hidden-polling test (see POLL_LIFECYCLE in the Makefile)"; exit 1;; esac; done

test-plugins: check-poll-coverage build/test-boost-deck build/test-shift-deck build/test-launch-deck $(foreach n,$(POLL_NAMES),test-plugins-$(n))
	./build/test-boost-deck display | diff - tests/golden/boost-deck-display.txt
	./build/test-shift-deck display | diff - tests/golden/shift-deck-display.txt
	./build/test-launch-deck display | diff - tests/golden/launch-deck-display.txt
	./build/test-boost-deck polling
	./build/test-shift-deck polling
	./build/test-launch-deck polling

launch-assets:
	$(PYTHON) ../tools/launch_art.py

# A plugin that reaps its worker must not forget the pid on ECHILD alone (the HMI may ignore SIGCHLD): every
# waitpid in src/plugins needs an ESRCH check beside it, so a new plugin cannot bring the respawn leak back.
check-reap:
	@for f in src/plugins/*.cpp; do w=$$(grep -c 'waitpid' $$f); e=$$(grep -c 'ESRCH' $$f); \
	  [ $$w -le $$e ] || { echo "$$f: waitpid without an ESRCH check (see reap() in shift_deck.cpp)"; exit 1; }; done

test: check-reap build/xdrive-control build/boost-deck build/shift-deck build/mhd-maps build/engine-codes build/test-core build/test-f150 build/test-most build/test-diag-link build/bootmod3 build/exhaust-flap build/test-launch-timer build/test-launch-records build/test-boost-preferences build/test-warm-progress build/warmdeck-collector build/mgflasher build/hybriddeck-collector
	./build/test-warm-progress
	./build/test-boost-preferences
	./build/test-launch-timer
	./build/test-launch-records
	./build/test-core
	./build/test-f150
	./build/test-most
	./build/test-diag-link
	$(PYTHON) -B tests/test_drive_apps.py
	$(PYTHON) -B tests/test_oneshot_most.py
	$(PYTHON) -B tests/test_transport.py
	$(MAKE) test-plugins
