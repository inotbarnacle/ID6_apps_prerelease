#ifndef ID6_LIBRARY_BOOST_FILES_H
#define ID6_LIBRARY_BOOST_FILES_H

#ifdef ID6_EMULATOR
#define ID6_BOOST_DATA_DIR "/dev/shmem/"
#else
#define ID6_BOOST_DATA_DIR "/var/opt/hmi/data/ID5/"
#endif
#define ID6_BOOST_PREFS ID6_BOOST_DATA_DIR "ID6LibraryLab.boost.prefs"
#define ID6_BOOST_LAYOUT ID6_BOOST_DATA_DIR "ID6LibraryLab.boost.signals"
#define ID6_BOOST_DEFAULTS ID6_BOOST_LAYOUT ".defaults"

#endif
