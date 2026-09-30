#ifndef __GC_ADAPTER_H__
#define __GC_ADAPTER_H__

// Support for Nintendo's GameCube controller adapter (WUP-028) through libusb, similar to Dolphin.
// Each controller plugged into the adapter is exposed as an SDL virtual game controller, so it works with the
// regular controller bindings, deadzone and rumble. The digital clicks at the end of the L and R triggers are
// exposed as separate buttons (SDL's paddle 1 and 2) in addition to the analog triggers.
//
// On Windows the adapter needs the WinUSB driver (installed with Zadig), the same setup as Dolphin.
namespace recomp::gc_adapter {
    // Starts the thread that connects to the adapter and reads its controllers.
    void start();
    // Stops the adapter thread and removes its controllers.
    void stop();
    // Adds and removes controllers and updates their state. Must be called from the thread that handles SDL events.
    void update();
}

#endif
