#ifndef DISPLAY_LIST_H
#define DISPLAY_LIST_H

#include "Math/math.h"
#include "Memory/segment.h"
#include <cstdint>

namespace GBI {
    struct Vtx {
        int16_t position[3];
        uint16_t flag;
        int16_t texture_coordinate[2];
        uint8_t coordinate_or_normal[4];
    };

    struct Vp {
        int16_t scale[4];
        int16_t translate[4];
    };

    struct Tri {
        uint8_t flag;
        uint8_t vertices[3];
    };

    typedef Mat4<int32_t> Mtx;

    struct Light {
        uint8_t diffuse[3];
        uint8_t diffuse_copy[3];
        int8_t direction[3]; //normalized
    };

    struct DLCommandSPNOOP {};

    struct DLCommandMTX {
        enum : uint8_t {
            MTX_PROJECTION = 0x01,
            MTX_LOAD = 0x02,
            MTX_PUSH = 0x04
        };

        uint8_t params;
        uint32_t seg_addr;
    };

    struct DLCommandMOVEMEM {};

    struct DLCommandVTX {
        uint8_t num;
        uint8_t index;
        uint16_t length;
        SegmentedAddress seg_addr;
    };

    struct DLCommandDL {
        uint8_t push;
        SegmentedAddress seg_addr;
    };

    struct DLCommandRDPHAlF {
        enum : uint8_t {
            HIGHER,
            LOWER,
            CONTINUE
        };

        uint8_t type;
        uint32_t value;
    };

    struct DLCommandGEOMETRYMODE {
        enum : uint8_t {
            CLEAR,
            SET
        };

        uint8_t type;
        uint32_t params;
    };

    struct RSPState {
        Mat4<int32_t> projection;
        std::vector<Mat4<int32_t>> matrix_stack;
        std::array<GBI::Vtx, 16> vertices;
    };
}

struct DisplayListNode {
    Mat4<int32_t> transform;
    SegmentedAddress display_list;
};

#endif /* DISPLAY_LIST_H */
