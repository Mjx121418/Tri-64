#include "Geo/test_geo.h"
#include "LevelScript/test_behavior_script.h"
#include "LevelScript/test_collision.h"
#include "LevelScript/test_level_script.h"
#include "LevelScript/test_object.h"
#include "Log.h"
#include "Memory/segment.h"
#include "Scripts/geo_layout.h"
#include "test_parallel.h"
#include "tree_printer.h"

#include <algorithm>
#include <functional>
#include <thread>
#include <vector>

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

    WarningLog warnings;
    GeoLayoutProcessor processor(seg_table, warnings);
    std::unique_ptr<GraphNode> root = processor.processGeoLayout(entry);
    printNodeTree(*root, 0);

    const std::vector<std::function<void()>> tests {
        testRunLevelScript,
        testRomManagerAreaBank,
        testFixedAddressMemory,
        testMopLoader,
        testLevelName,
        testObjectModels,
        testLevelScriptData,
        testDlRspData,
        testTextureFormats,
        testCollision,
        testBehaviorScript,
        testObject,
        testBillboardSplit,
        testDisplayList,
        testMatrixSupport,
        testExportObj,
        testHackRobustness,
    };

    // Each test owns its ROM/state, so independent extraction tests can share
    // the available cores. Output may be interleaved, but failures remain visible.
    const unsigned hardware_threads = std::thread::hardware_concurrency();
    const size_t worker_count = std::min(
        tests.size(), static_cast<size_t>(hardware_threads == 0 ? 1 : hardware_threads));
    TestParallel::Executor executor(worker_count);
    TestParallel::setExecutor(&executor);
    executor.run(tests);
    TestParallel::setExecutor(nullptr);
    return 0;
}
