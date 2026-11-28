# Called by native/build.sh from the full-screen project root.
build_companions() {
    standard=$1
    while read -r name original kind; do
        case "$original" in
            Bootmod3Suite) plugin_source=bootmod3_suite worker_source=bootmod3_suite_worker worker=bootmod3-qnx ;;
            MgFlasher) plugin_source=mg_flasher worker_source=mg_flasher_worker worker=mgflasher-qnx ;;
            XDriveControl) plugin_source=xdrive_control worker_source=xdrive_control_worker worker=xdrive-control-qnx ;;
            HybridDeck) plugin_source=hybrid_deck worker_source=hybrid_deck_worker worker=hybriddeck-collector-qnx ;;
            *) return 1 ;;
        esac
        mkdir -p "native/out/$name"
        qcc -V4.4.2,gcc_ntoarmv7le_gpp -lang-c++ -Wc,-std=gnu++98 -mno-unaligned-access -Wall -Wextra -Werror \
            -D_QNX_SOURCE -DID6_COMPANION "-DID6_MENU_NAME=\"$name\"" "-DID6_MENU_SURFACE=$kind" \
            -Inative/src -Inative/generated -Inative/sdk -I"$standard/src/common" -I"$standard/src/ipc" \
            -O2 -fno-exceptions -fPIC -shared "$standard/src/plugins/$plugin_source.cpp" native/out/gauge_core.o \
            "$NBTEVO_FRAMEWORKS_DIR/libAppFramework.A.so" "$NBTEVO_FRAMEWORKS_DIR/libCoreFramework.A.so" \
            -Lnative/out/link -lsocket -lm -Wl,--allow-shlib-undefined "-Wl,-soname,lib$name.so" \
            -o "native/out/$name/lib$name.so"
        c -I"$standard/src/common" -I"$standard/src/ipc" -I"$standard/src/workers" -O2 \
            "$standard/src/workers/$worker_source.c" native/out/gauge_core.o "$standard/src/common/diag_link.c" \
            "$standard"/src/common/most/*.c -Lnative/out/link -lm -lsocket -o "native/out/$name/$worker"
        cp "native/generated/$name.manifest" "native/generated/$name.xml" "native/out/$name/"
    done < native/generated/companions.txt
}
