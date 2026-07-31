#include "godot_bridge.h"

void GodotBridge::_bind_methods() {
    ClassDB::bind_method(D_METHOD("loadROM", "path"), &GodotBridge::loadROM);
    ClassDB::bind_method(D_METHOD("ROMLoaded"), &GodotBridge::ROMLoaded);
}

GodotBridge::GodotBridge() {
}

void GodotBridge::loadROM(String path) {
    std::string utf8_path = path.utf8().get_data();
    std::filesystem::path fs_path(std::u8string(reinterpret_cast<const char8_t *>(utf8_path.data()), utf8_path.size()));
    rom.load(fs_path);
}

bool GodotBridge::ROMLoaded() {
    return rom.is_loaded;
}
