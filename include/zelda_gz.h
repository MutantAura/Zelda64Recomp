#ifndef __ZELDA_GZ_H__
#define __ZELDA_GZ_H__

#include <cstdint>

#include "recomp.h"

// Support for gz, the Ocarina of Time practice ROM, which is built into Ocarina64Recompiled when it's configured with
// ZELDA64_OOT_GZ. gz's hooks are recompiled into the game, and do nothing unless the game is started with
// "Start gz" in the launcher.
namespace zelda64::gz {
    // Whether gz is built into this executable.
    bool available();
    // Sets whether gz runs in the next game started from the launcher.
    void set_started(bool started);
    // Whether gz is running.
    bool started();
    // Loads gz into memory. Called before the game's entrypoint runs.
    void on_init(uint8_t* rdram, recomp_context* ctx);
}

#endif
