#ifndef ROM_H
#define ROM_H

#include <cstdint>
#include <filesystem>
#include <vector>

struct ROM {
    const static int MAX_SIZE { 1 << 26 };

    bool is_loaded { false };
    int size { 0 };
    std::vector<uint8_t> data;

    void load(std::filesystem::path path);
    ~ROM();
};

#endif /* ROM_H */
