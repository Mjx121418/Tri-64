#ifndef GODOT_BRIDGE_H
#define GODOT_BRIDGE_H

#include <godot_cpp/classes/ref_counted.hpp>
#include "ROM.h"

using namespace godot;

class GodotBridge : public RefCounted {
    GDCLASS(GodotBridge, RefCounted);

private:
    ROM rom;

protected:
    static void _bind_methods();

public:
    void loadROM(String path);
    bool ROMLoaded();
    GodotBridge();
}; 

#endif /* GODOT_BRIDGE_H */
