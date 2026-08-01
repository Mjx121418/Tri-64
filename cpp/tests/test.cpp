#include "Geo/test_geo.h"
#include "LevelScript/test_level_script.h"
#include "Memory/segment.h"
#include "Scripts/geo_layout.h"
#include "tree_printer.h"

int main() {
    printf("Hi.\n");
    SegmentTable seg_table;
    seg_table.rom_span = std::span(sm64_geo_fixture::segment_0E);
    seg_table.loadSegment(0x0E, 0, sm64_geo_fixture::segment_0E.size());
    seg_table.rom_span = std::span(sm64_geo_fixture::segment_0F);
    seg_table.loadSegment(0x0F, 0, sm64_geo_fixture::segment_0F.size());
    seg_table.rom_span = std::span(sm64_geo_fixture::segment_10);
    seg_table.loadSegment(0x10, 0, sm64_geo_fixture::segment_10.size());
    SegmentedAddress entry {0x0E, 0};

    GeoLayoutProcessor processor(seg_table);
    std::unique_ptr<GraphNode> root = processor.processGeoLayout(entry);
    printNodeTree(*root, 0);

    testRunLevelScript();
    testDisplayList();
    return 0;
}