// Host stand-in for the BMW plugin framework, just enough to run a plugin's own logic.
// Build with -fno-access-control (the fake reaches into the SDK's private members).
#ifndef FAKE_BMW_H
#define FAKE_BMW_H

#include "bmw_list.h"
#include <string>
#include <vector>

// One call the plugin made to updateModel: kind 'i' image id, 's' text, 'n' number.
struct FakeUpdate {
    unsigned model;
    char kind;
    unsigned number;
    std::string text;
};

// A timer message the plugin posted to itself and has not yet been delivered.
struct FakePost {
    unsigned id;
    std::vector<unsigned> words;
    unsigned delay;
};

extern std::vector<FakeUpdate> fake_updates;
extern std::vector<FakePost> fake_posts;

// Runs the handler registered with addHandler(state, action) (state 0 = no state).
bool fake_fire(unsigned state, unsigned action, const bmw::Parcel& event);
void fake_reset();

#endif
