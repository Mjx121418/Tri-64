#include <fstream>

#include "ROM.h"

void ROM::load(std::filesystem::path path) {
    if (std::filesystem::exists(path)) {
        int size = std::filesystem::file_size(path);
        if (size > MAX_SIZE) {
            is_loaded = false;
            return;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            is_loaded = false;
            return;
        }

        data = std::vector<uint8_t>(size);
        file.read(reinterpret_cast<char *>(data.data()), size);
        is_loaded = true;
    } else {
        is_loaded = false;
        return;
    }
}

ROM::~ROM() {
    
}