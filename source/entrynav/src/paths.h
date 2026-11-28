#ifndef ENTRYNAV_APPS_PATHS_H
#define ENTRYNAV_APPS_PATHS_H

/* Where the apps live on the EntryNav2. HmiMain3D loads plugins from /opt/hmi/data/ro/Plugins. The workers sit in the
 * same folders because the data partition (/var/local) is mounted noexec. Settings and the gateway address go on the
 * data partition, which stays writable. */
#define ENA_PLUGIN_DIR "/opt/hmi/data/ro/Plugins"
#define ENA_DATA_DIR "/var/local/data/EntryNavApps"
#define ENA_GATEWAY_FILE ENA_DATA_DIR "/gateway-ip.txt"

#define ENA_BOOSTDECK_WORKER ENA_PLUGIN_DIR "/BoostDeck/boostdeck-collector"
#define ENA_SHIFTDECK_WORKER ENA_PLUGIN_DIR "/ShiftDeck/shift-deck"
#define ENA_BM3MAPS_WORKER ENA_PLUGIN_DIR "/Bm3Maps/bm3-maps"
#define ENA_ENGINECODES_WORKER ENA_PLUGIN_DIR "/EngineCodes/engine-codes"
#define ENA_MHDMAPS_WORKER ENA_PLUGIN_DIR "/MhdMaps/mhd-maps"

#endif
