#ifndef LEVEL_EXTRACT_H
#define LEVEL_EXTRACT_H

#include "Level/Object.h"
#include "Level/dl_interpreter.h"
#include "Level/texture.h"
#include "Log.h"
#include "Memory/segment.h"
#include "ROM.h"
#include "Scripts/Collision.h"
#include "Scripts/movtex.h"
#include "Scripts/skybox.h"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// 在引擎内直接提取关卡几何（无需先导出 OBJ）。
//
// 流程与 tests/LevelScript 的导出一致：定位脚本段 → 运行关卡脚本（按
// LevelNum 分发到目标关卡）→ 遍历该关卡的 Area 场景图收集 DL → DL 解释器
// 生成网格 → 跨 DL 合并（复用 GBI::Mesh/Material，按内容 + 纹理源图像去重）
// → 解码每材质纹理为 RGBA8（复用 GBI::Texture）。
// 对象模型解码与 MACRO_OBJECTS 展开在 ObjectExtract（Level/Object.*）完成。
namespace LevelExtract {

struct Result {
    bool ok {false};
    std::string error;
    GBI::Mesh mesh;                      // 合并后的网格（含材质表与纹理源图像）
    std::vector<GBI::Texture> textures;  // 与 mesh.materials 并行：解码纹理
    std::vector<ObjectExtract::Object> objects; // 统一对象（OBJECT 命令 + 宏 +
                                                // 特殊对象变换而来；行为脚本的作用对象）
    std::map<int16_t, ObjectExtract::ObjectModel> object_models; // 对象模型缓存（按 model id 去重）
    std::vector<Collision::Data> object_collisions; // 与 objects 并行：各对象行为
                                                    // LOAD_COLLISION_DATA 的碰撞（本地空间）
    Movtex::Data movtex;          // 移动纹理四边形（水/熔岩表面，Y 由水盒补充）
    Collision::Data collision;          // 解码后的碰撞数据（表面/顶点/水盒/房间/特殊对象）
    std::vector<Warning> warnings;      // 提取过程中的警告/被守卫的异常（越界数据、
                                        // 跳过的模型/几何、解码失败等），供 UI 弹窗展示
    std::vector<int> areas;              // 该关卡所有有效区域索引（有 root_node）
    std::string level_name;             // 从 ROM 段2提取的关卡名称（可为空）
    Vec3<float> mario_start_pos {};     // Mario 的初始位置（关卡脚本 cmdSetMarioStartPos）
    float mario_start_angle_y {0};      // Mario 的初始朝向（Y 轴旋转角度，弧度）
    // 区域背景（geo 0x19 节点）：天空盒 id + 生成函数，或纯色填充（见 Skybox）。
    Skybox::Background background {};
    // 天空盒贴图集（解码自段 0x0A；纯色背景时为空）。渲染端按 skybox.c 的
    // yaw/pitch 贴图滚动逐帧绘制。
    Skybox::SkyboxImage skybox {};

    // --- 关卡脚本记录的区域/关卡级数据（镜像 Level/Area，暂不渲染） ---
    std::vector<WarpNode> warp_nodes;          // 0x26 传送节点
    std::vector<WarpNode> painting_warp_nodes; // 0x27 画框传送节点
    std::array<InstantWarp, 4> instant_warps {}; // 0x28 即时传送
    std::vector<Whirlpool> whirlpools;         // 0x3B 漩涡
    uint8_t dialog[2] {0, 0};                  // 0x30 区域对话框
    uint16_t music_param {0};                  // 0x36 背景音乐 settingsPreset
    uint16_t music_param2 {0};                 // 0x36 背景音乐 seq
    int16_t unused_area_28[5] {0, 0, 0, 0, 0}; // 0x3A 未使用
    // 区域相机数据（geo views[0] 的 NODE_CAMERA；按值拷贝，不持有场景图指针）。
    std::optional<GraphNodeCamera> camera {};
    int16_t mario_model_id {0};                // 0x25 Mario 出生模型
    uint32_t mario_behavior_arg {0};           // 0x25 Mario 出生参数
    SegmentedAddress mario_behavior_script {}; // 0x25 Mario 出生行为
    WarpTransition transition {};              // 0x33 过渡设置
};

// 关卡提取管线（镜像 LevelScriptVM 的结构）：绑定 ROM，run(level_num,
// area_index) 重置并执行完整提取，结果经 result() 取得。ROM 数据在
// run 调用期间必须保持有效（SegmentTable 的 rom_span 指向 ROM::data）。
class LevelExtractor {
    struct DisplayListRef {
        SegmentedAddress dl;
        uint8_t layer {0};
    };

    struct DisplayListCollection {
        std::vector<DisplayListRef> lists;
        const GraphNodeBackGround *background {nullptr};
    };

    ROM &rom_;
    SegmentTable seg_table_;
    Level level_;
    Result result_;
    bool ok_ {false};
    std::string error_;
    WarningLog log_; // 本次提取的警告/被守卫的异常（run 时清空，结束后拷入 result_）

    static size_t findPattern(const std::vector<uint8_t> &rom,
                              const std::array<uint8_t, 4> &pattern, size_t from = 0);
    static uint32_t readBE32(const uint8_t *p);
    static std::string decodeSM64String(const uint8_t *data, size_t max_len);
    static int32_t levelNumToCourseNum(int32_t level_num);
    static DisplayListCollection collectDisplayLists(const GraphNode &root);

    size_t findScriptsStart() const;
    void loadSegment2();
    void loadCourseNameSegment();
    std::string readCourseName(int32_t level_num) const;
    std::vector<int> validAreaIndices() const;
    bool loadMainSegment();

    // 定位脚本段、可选加载补充段、运行目标关卡的关卡脚本，构建段表与场景图。
    void runLevelScript(int level_num, bool load_supplemental);
    void runScriptInternal(int level_num, bool load_supplemental);
    // Mirrors the runtime load_area() bank side effect for the selected area.
    void loadRomManagerAreaSegment(int area_index);
    // 提取 area_index 区域的几何/对象/碰撞（run 的第二步；越界数据抛异常时
    // run 转成 result_.error）。
    void extractArea(int level_num, int area_index);

public:
    explicit LevelExtractor(ROM &rom) : rom_(rom) {}

    static Result extract(ROM &rom, int level_num, int area_index);
    static std::vector<int> listAreas(ROM &rom, int level_num);
    static std::string extractLevelName(ROM &rom, int level_num);
    static std::map<int, std::string> loadAllLevelNames(ROM &rom);
    static bool loadMainSegment(SegmentTable &seg_table,
                                const std::vector<uint8_t> &rom);

    // 只运行关卡脚本并返回有效区域，不解码区域几何、纹理或对象模型。
    void runScript(int level_num);

    // 提取 rom 中 level_num（LevelNum，如 BOB=9）的 area_index 号区域。
    void run(int level_num, int area_index);

    const Result &result() const { return result_; }
    // Move the completed result out when the extractor will not be reused.
    // This avoids copying meshes, textures, collision data, and object models.
    Result takeResult() { return std::move(result_); }
};

// 便捷包装：一次完整提取（供 bridge/测试使用）。
Result extract(ROM &rom, int level_num, int area_index);

// 返回 level_num 关卡的所有有效区域索引（供 UI 的 Area 下拉列表使用）。
std::vector<int> listAreas(ROM &rom, int level_num);

// 仅提取关卡名称（从 seg2_course_name_table），不进行完整的几何提取。
std::string extractLevelName(ROM &rom, int level_num);

// 一次性加载 segment 2 并提取所有已知关卡的名称，无需运行关卡脚本。
// 返回 LevelNum → 名称 的映射，提取失败的关卡不在映射中。
std::map<int, std::string> loadAllLevelNames(ROM &rom);

// 载入游戏的主段（段 0x00）到 seg_table.seg 0。主段是游戏代码+数据段，由
// 启动入口 DMA 到 RDRAM 0x80200000（ROM 0x1000 起），关卡脚本不加载它。
// 段 0 按"主段内部偏移"线性载入：段 0 偏移 0 = 主段起始。返回是否成功。
// 宏/特殊对象 preset 表就位于主段内（见 Scripts/preset_tables.h）。
bool loadMainSegment(SegmentTable &seg_table, const std::vector<uint8_t> &rom);

} // namespace LevelExtract

#endif /* LEVEL_EXTRACT_H */
