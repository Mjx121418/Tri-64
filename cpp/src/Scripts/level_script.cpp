#include "level_script.h"
#include <bit>
#include <cstdio>

namespace {

// Matches enum ScriptStatus in the decomp's level_script.c.
constexpr int32_t SCRIPT_RUNNING = 1;
constexpr int32_t SCRIPT_PAUSED = 0;

// Safety net: the game's own boot script loops forever; a static export never
// wants that, so any script running past this many commands is aborted.
constexpr int64_t MAX_INSTRUCTIONS = 10'000'000;

}

LevelScriptVM::LevelScriptVM(SegmentTable &seg_table, Level &level) :
    level(level),
    geo_layout_processor(GeoLayoutProcessor(seg_table)),
    seg_table(seg_table) {
}

void LevelScriptVM::execute(SegmentedAddress entry) {
    command_stack.clear();
    frame_stack.clear();
    argument_stack.clear();
    reg = 0;
    current_area_index = -1;
    script_status = SCRIPT_RUNNING;

    pc = entry;
    getNextCommand();

    int64_t executed = 0;
    while (script_status == SCRIPT_RUNNING) {
        if (++executed > MAX_INSTRUCTIONS) {
            printf("LevelScriptVM: script did not terminate after %lld instructions, aborting.\n", (long long)executed);
            break;
        }
        ExecuteCommand();
    }
}

void LevelScriptVM::getNextCommand() {
    current_command.addr = pc;
    current_command.opcode = seg_table.read(pc);
    current_command.length = seg_table.read(pc, 1);
    current_command.data = seg_table.data(pc, current_command.length);
    pc.offset += current_command.length;
}

void LevelScriptVM::ExecuteCommand() {
    switch (current_command.opcode) {
        case 0x00: cmdLoadAndExcute(); break;
        case 0x01: cmdExitAndExecute(); break;
        case 0x02: cmdExit(); break;
        case 0x03: cmdSleep(); break;
        case 0x04: cmdSleep2(); break;
        case 0x05: cmdJump(); break;
        case 0x06: cmdJumpAndLink(); break;
        case 0x07: cmdReturn(); break;
        case 0x08: cmdJumpAndLinkPushArg(); break;
        case 0x09: cmdJumpRepeat(); break;
        case 0x0A: cmdLoopBegin(); break;
        case 0x0B: cmdLoopUntil(); break;
        case 0x0C: cmdJumpIf(); break;
        case 0x0D: cmdJumpAndLinkIf(); break;
        case 0x0E: cmdSkipIf(); break;
        case 0x0F: cmdSkip(); break;
        case 0x10: cmdSkippableNOP(); break;
        case 0x11: cmdCall(); break;
        case 0x12: cmdCallLoop(); break;
        case 0x13: cmdSetRegister(); break;
        case 0x14: cmdPushPoolState(); break;
        case 0x15: cmdPopPoolState(); break;
        case 0x16: cmdLoadToFixedAddress(); break;
        case 0x17: cmdLoadRaw(); break;
        case 0x18: cmdLoadMIO0(); break;
        case 0x19: cmdLoadMarioHead(); break;
        case 0x1A: cmdLoadMIO0Texture(); break;
        case 0x1B: cmdInitLevel(); break;
        case 0x1C: cmdClearLevel(); break;
        case 0x1D: cmdAllocLevelPool(); break;
        case 0x1E: cmdFreeLevelPool(); break;
        case 0x1F: cmdBeginArea(); break;
        case 0x20: cmdEndArea(); break;
        case 0x21: cmdLoadModelFromDL(); break;
        case 0x22: cmdLoadModelFromGeo(); break;
        case 0x23: cmd23(); break;
        case 0x24: cmdPlaceObject(); break;
        case 0x25: cmdInitMario(); break;
        case 0x26: cmdCreateWarpNode(); break;
        case 0x27: cmdCreatePaintingWarpNode(); break;
        case 0x28: cmdCreateInstantWarp(); break;
        case 0x29: cmdLoadArea(); break;
        case 0x2A: cmdUnloadArea(); break;
        case 0x2B: cmdSetMarioStartPos(); break;
        case 0x2C: cmd2C(); break;
        case 0x2D: cmd2D(); break;
        case 0x2E: cmdSetTerrainData(); break;
        case 0x2F: cmdSetRooms(); break;
        case 0x30: cmdShowDialog(); break;
        case 0x31: cmdSetTerrainType(); break;
        case 0x32: cmdNOP(); break;
        case 0x33: cmdSetTransition(); break;
        case 0x34: cmdSetBlackout(); break;
        case 0x35: cmdSetGamma(); break;
        case 0x36: cmdSetMusic(); break;
        case 0x37: cmdSetMenuMusic(); break;
        case 0x38: cmd38(); break;
        case 0x39: cmdSetMacroObjects(); break;
        case 0x3A: cmd3A(); break;
        case 0x3B: cmdCreateWhirlpool(); break;
        case 0x3C: cmdGetOrSetVar(); break;
        default:
            printf("LevelScriptVM: unknown command 0x%02x at seg %02x offset %07x, aborting.\n",
                       current_command.opcode, current_command.addr.seg, current_command.addr.offset);
            script_status = SCRIPT_PAUSED;
    }
}

int32_t LevelScriptVM::evalScriptOp(int8_t op, int32_t arg)
{
    int32_t result = 0;

    switch (op) {
    case 0:
        result = reg & arg;
        break;
    case 1:
        result = !(reg & arg);
        break;
    case 2:
        result = reg == arg;
        break;
    case 3:
        result = reg != arg;
        break;
    case 4:
        result = reg < arg;
        break;
    case 5:
        result = reg <= arg;
        break;
    case 6:
        result = reg > arg;
        break;
    case 7:
        result = reg >= arg;
        break;
    }

    return result;
}

void LevelScriptVM::cmdLoadAndExcute() {
    int16_t seg = current_command.cmdGet<int16_t>(2);
    uint32_t rom_start = current_command.cmdGet<uint32_t>(4);
    uint32_t rom_end = current_command.cmdGet<uint32_t>(8);
    uint32_t entry = current_command.cmdGet<uint32_t>(12);

    seg_table.loadSegment(seg, rom_start, rom_end);
    getNextCommand();
    command_stack.push_back(current_command);
    frame_stack.push_back(command_stack.size());
    pc.setAddress(entry);
    getNextCommand();
}

void LevelScriptVM::cmdExitAndExecute() {
    int16_t seg = current_command.cmdGet<int16_t>(2);
    uint32_t rom_start = current_command.cmdGet<uint32_t>(4);
    uint32_t rom_end = current_command.cmdGet<uint32_t>(8);
    uint32_t entry = current_command.cmdGet<uint32_t>(12);

    seg_table.loadSegment(seg, rom_start, rom_end);
    if (!frame_stack.empty()) {
        command_stack.resize(frame_stack.back());
        frame_stack.pop_back();
    }
    pc.setAddress(entry);
    getNextCommand();
}

void LevelScriptVM::cmdExit() {
    if (frame_stack.empty()) {
        // The top-level script ended.
        script_status = SCRIPT_PAUSED;
        return;
    }

    command_stack.resize(frame_stack.back());
    frame_stack.pop_back();
    current_command = command_stack.back();
    command_stack.pop_back();
    pc = current_command.addr;
    pc.offset += current_command.length;
}

void LevelScriptVM::cmdSleep() {
    // In the game this pauses the script for N frames; a static export runs to
    // completion in one pass, so the delay is skipped.
    getNextCommand();
}

void LevelScriptVM::cmdSleep2() {
    getNextCommand();
}

void LevelScriptVM::cmdJump() {
    uint32_t seg_addr = current_command.cmdGet<uint32_t>(4);
    pc = segAddress(seg_addr);
    getNextCommand();
}

void LevelScriptVM::cmdJumpAndLink(){
    uint32_t seg_addr = current_command.cmdGet<uint32_t>(4);
    getNextCommand();
    command_stack.push_back(current_command);
    pc = segAddress(seg_addr);
    getNextCommand();
}

void LevelScriptVM::cmdReturn() {
    current_command = command_stack.back();
    command_stack.pop_back();
    pc = current_command.addr;
    pc.offset += current_command.length;
}

void LevelScriptVM::cmdJumpAndLinkPushArg() {
    argument_stack.push_back(current_command.cmdGet<int16_t>(2));
    getNextCommand();
    command_stack.push_back(current_command);
}

void LevelScriptVM::cmdJumpRepeat() {
    int32_t val = argument_stack.back();

    if (val == 0) {
        current_command = command_stack.back();
        pc = current_command.addr;
        pc.offset += current_command.length;
    } else if (--val != 0) {
        argument_stack.back() = val;
        current_command = command_stack.back();
        pc = current_command.addr;
        pc.offset += current_command.length;
    } else {
        getNextCommand();
        argument_stack.pop_back();
        command_stack.pop_back();
    }
}

void LevelScriptVM::cmdLoopBegin() {
    argument_stack.push_back(0);
    getNextCommand();
    command_stack.push_back(current_command);
}

void LevelScriptVM::cmdLoopUntil() {
    uint8_t op = current_command.cmdGet<uint8_t>(2);
    int32_t arg = current_command.cmdGet<int32_t>(4);
    if (evalScriptOp(op, arg) != 0) {
        getNextCommand();
        command_stack.pop_back();
        argument_stack.pop_back();
    } else {
        current_command = command_stack.back();
    }
}

void LevelScriptVM::cmdJumpIf() {
    uint8_t op = current_command.cmdGet<uint8_t>(2);
    int32_t arg = current_command.cmdGet<int32_t>(4);
    if (evalScriptOp(op, arg) != 0) {
        uint32_t seg_addr = current_command.cmdGet<uint32_t>(8);
        pc.setAddress(seg_addr);
        getNextCommand();
    } else {
        getNextCommand();
    }
}

void LevelScriptVM::cmdJumpAndLinkIf() {
    uint8_t op = current_command.cmdGet<uint8_t>(2);
    int32_t arg = current_command.cmdGet<int32_t>(4);
    if (evalScriptOp(op, arg) != 0) {
        uint32_t seg_addr = current_command.cmdGet<uint32_t>(8);
        getNextCommand();
        command_stack.push_back(current_command);
        pc.setAddress(seg_addr);
        getNextCommand();
    } else {
        getNextCommand();
    }
}

void LevelScriptVM::cmdSkipIf() {
    uint8_t op = current_command.cmdGet<uint8_t>(2);
    int32_t arg = current_command.cmdGet<int32_t>(4);
    if (evalScriptOp(op, arg) == 0) {
        do {
            getNextCommand();
        } while (current_command.opcode == 0x0F || current_command.opcode == 0x10);
    }

    getNextCommand();
}

void LevelScriptVM::cmdSkip() {
    do {
        getNextCommand();
    } while (current_command.opcode == 0x10);

    getNextCommand();
}

void LevelScriptVM::cmdSkippableNOP() {
    getNextCommand();
}

void LevelScriptVM::cmdCall() {
    // The decomp calls a MIPS function here to compute a new value for `reg`.
    // We cannot execute MIPS code, and for a static export the result is
    // irrelevant (it is only used by the conditionals above), so skip the call.
    getNextCommand();
}

void LevelScriptVM::cmdCallLoop() {
    // In the game this repeatedly calls a MIPS function until it returns
    // nonzero, pausing the script in the meantime. Every level script ends
    // with CALL_LOOP: the level is fully loaded by then, so a static export
    // stops here (mirrors the game's "level is loaded and being rendered").
    script_status = SCRIPT_PAUSED;
}

void LevelScriptVM::cmdSetRegister() {
    reg = current_command.cmdGet<int16_t>(2);
    getNextCommand();
}

void LevelScriptVM::cmdPushPoolState() {
    // Memory pool management, irrelevant for a static export.
    getNextCommand();
}

void LevelScriptVM::cmdPopPoolState() {
    getNextCommand();
}

void LevelScriptVM::cmdLoadToFixedAddress() {
    // Requires accurate emulation of memory allocation; unused by vanilla
    // course scripts (only the menu/goddard screens use it).
    getNextCommand();
}

void LevelScriptVM::cmdLoadRaw() {
    int16_t seg = current_command.cmdGet<int16_t>(2);
    uint32_t rom_start = current_command.cmdGet<uint32_t>(4);
    uint32_t rom_end = current_command.cmdGet<uint32_t>(8);

    // workaround for the custom loading code used by sm64 level editor.
    if (seg >= 0x100 && seg < 0x120) {
        seg -= 0x100;
    }

    seg_table.loadSegment(seg, rom_start, rom_end);

    getNextCommand();
}

void LevelScriptVM::cmdLoadMIO0() {
    int16_t seg = current_command.cmdGet<int16_t>(2);
    uint32_t rom_start = current_command.cmdGet<uint32_t>(4);
    uint32_t rom_end = current_command.cmdGet<uint32_t>(8);
    if (!seg_table.loadMIO0Segment(seg, rom_start, rom_end)) {
        printf("Bad MIO0 segment at 0x%x\n", seg);
    }

    getNextCommand();
}

void LevelScriptVM::cmdLoadMarioHead() {
    // Goddard face animation; irrelevant for a static export.
    getNextCommand();
}

void LevelScriptVM::cmdLoadMIO0Texture() {
    int16_t seg = current_command.cmdGet<int16_t>(2);
    uint32_t rom_start = current_command.cmdGet<uint32_t>(4);
    uint32_t rom_end = current_command.cmdGet<uint32_t>(8);
    if (!seg_table.loadMIO0Segment(seg, rom_start, rom_end)) {
        printf("Bad MIO0 segment at 0x%x\n", seg);
    }

    getNextCommand();
}

void LevelScriptVM::cmdInitLevel() {
    // Mirrors the decomp's init_graph_node_start(NULL, &gObjParentGraphNode)
    // plus clear_objects() and clear_areas().
    level.object_parent = GraphNodeStart {};
    for (size_t i = 0; i < level.areas.size(); i++) {
        level.areas[i].index = static_cast<int8_t>(i);
        level.areas[i].flag = 0;
        level.areas[i].root_node.reset();
        level.areas[i].object_infos.clear();
        level.areas[i].macro_objects.clear();
        level.areas[i].terrain_addr = {};
        level.areas[i].rooms_addr = {};
    }

    getNextCommand();
}

void LevelScriptVM::cmdClearLevel() {
    // Mirrors clear_area_graph_nodes() + clear_areas(). In the game this only
    // runs when the player leaves the level, after rendering has stopped.
    for (auto &area : level.areas) {
        area.root_node.reset();
        area.object_infos.clear();
        area.macro_objects.clear();
        area.terrain_addr = {};
        area.rooms_addr = {};
    }

    getNextCommand();
}

void LevelScriptVM::cmdAllocLevelPool() {
    getNextCommand();
}

void LevelScriptVM::cmdFreeLevelPool() {
    getNextCommand();
}

void LevelScriptVM::cmdBeginArea() {
    uint8_t area_id = current_command.cmdGet<uint8_t>(2);
    uint32_t geo_layout_addr = current_command.cmdGet<uint32_t>(4);

    if (area_id < 8) {
        level.areas[area_id].root_node = std::move(geo_layout_processor.processGeoLayout(segAddress(geo_layout_addr)));
        current_area_index = area_id;
    }

    getNextCommand();
}

void LevelScriptVM::cmdEndArea() {
    current_area_index = -1;
    getNextCommand();
}

void LevelScriptVM::cmdLoadModelFromDL() {
    uint16_t val12 = current_command.cmdGet<uint16_t>(2);
    int16_t val1 = val12 & 0x0FFF;
    int16_t val2 = val12 >> 12;
    uint32_t val3 = current_command.cmdGet<uint32_t>(4);

    if (val1 < 256) {
        SegmentedAddress seg_addr {val2, val3};
        std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
        SegmentedAddress display_list;

        display_list.setAddress(val3);

        node->flags = (val2 << 8) | GRAPH_RENDER_ACTIVE;
        node->data = GraphNodeDisplayList {display_list};

        level.loaded_graph_node[val1] = std::move(node);
    }

    getNextCommand();
}

void LevelScriptVM::cmdLoadModelFromGeo() {
    int16_t arg0 = current_command.cmdGet<int16_t>(2);
    uint32_t arg1 = current_command.cmdGet<uint32_t>(4);

    if (arg0 < 256) {
        SegmentedAddress seg_addr;
        seg_addr.setAddress(arg1);
        level.loaded_graph_node[arg0] = geo_layout_processor.processGeoLayout(seg_addr);
    }

    getNextCommand();
}

void LevelScriptVM::cmd23() {
    // GraphNodeScale variant of LOAD_MODEL_FROM_DL; unused in vanilla, kept
    // for fidelity with the decomp.
    uint16_t val12 = current_command.cmdGet<uint16_t>(2);
    int16_t model = val12 & 0x0FFF;
    int16_t layer = val12 >> 12;
    uint32_t dl_addr = current_command.cmdGet<uint32_t>(4);
    float scale = std::bit_cast<float>(current_command.cmdGet<int32_t>(8));

    if (model < 256) {
        std::unique_ptr<GraphNode> node = std::make_unique<GraphNode>();
        SegmentedAddress display_list;
        display_list.setAddress(dl_addr);

        node->flags = (layer << 8) | GRAPH_RENDER_ACTIVE;
        node->data = GraphNodeScale {display_list, scale};

        level.loaded_graph_node[model] = std::move(node);
    }

    getNextCommand();
}

void LevelScriptVM::cmdInitMario() {
    getNextCommand();
}

void LevelScriptVM::cmdPlaceObject() {
    uint8_t acts = current_command.cmdGet<uint8_t>(2);

    // The game only spawns objects whose act mask contains the current act;
    // 0x1F ("ALL_ACTS") always spawns. A static export keeps the first act.
    uint8_t act_mask = 1u << (curr_act_num - 1);

    if (current_area_index != -1 && ((acts & act_mask) || acts == 0x1F)) {
        ObjectSpawnInfo info;
        info.model_id = current_command.cmdGet<uint8_t>(3); // OBJECT 命令第 3 字节
        info.start_pos = readVec3s(current_command.data, 4);
        info.start_angle.x = (readInt<int16_t>(current_command.data, 10) * 0x8000) / 180;
        info.start_angle.y = (readInt<int16_t>(current_command.data, 12) * 0x8000) / 180;
        info.start_angle.z = (readInt<int16_t>(current_command.data, 14) * 0x8000) / 180;
        info.area_index = current_area_index;
        info.active_area_index = current_area_index;
        info.behavior_arg = current_command.cmdGet<uint32_t>(16);
        info.behavior_script = segAddress(current_command.cmdGet<uint32_t>(20));

        level.areas[current_area_index].object_infos.push_back(info);
    }

    getNextCommand();
}

void LevelScriptVM::cmdCreateWarpNode() {
    getNextCommand();
}

void LevelScriptVM::cmdCreatePaintingWarpNode() {
    getNextCommand();
}

void LevelScriptVM::cmdCreateInstantWarp() {
    getNextCommand();
}

void LevelScriptVM::cmdLoadArea() {
    getNextCommand();
}

void LevelScriptVM::cmdUnloadArea() {
    getNextCommand();
}

void LevelScriptVM::cmdSetMarioStartPos() {
    level.mario_start_area = current_command.cmdGet<uint8_t>(2);
    level.mario_start_angle_y = current_command.cmdGet<int16_t>(4) * 0x8000 / 180;
    level.mario_start_pos = readVec3s(current_command.data, 6);
    getNextCommand();
}

void LevelScriptVM::cmd2C() {
    getNextCommand();
}

void LevelScriptVM::cmd2D() {
    getNextCommand();
}

void LevelScriptVM::cmdSetTerrainData() {
    // TERRAIN(terrainData)：记录碰撞数据地址（解码在 Collision 模块完成）。
    if (current_area_index != -1) {
        level.areas[current_area_index].terrain_addr =
            segAddress(current_command.cmdGet<uint32_t>(4));
    }
    getNextCommand();
}

void LevelScriptVM::cmdSetRooms() {
    // ROOMS(surfaceRooms)：记录房间列表地址（s8，按静态表面顺序分配）。
    if (current_area_index != -1) {
        level.areas[current_area_index].rooms_addr =
            segAddress(current_command.cmdGet<uint32_t>(4));
    }
    getNextCommand();
}

void LevelScriptVM::cmdShowDialog() {
    getNextCommand();
}

void LevelScriptVM::cmdSetTerrainType() {
    if (current_area_index != -1) {
        level.areas[current_area_index].terrianType |= current_command.cmdGet<uint16_t>(2);
    }

    getNextCommand();
}

void LevelScriptVM::cmdNOP() {
    getNextCommand();
}

void LevelScriptVM::cmdSetTransition() {
    getNextCommand();
}

void LevelScriptVM::cmdSetBlackout() {
    getNextCommand();
}

void LevelScriptVM::cmdSetGamma() {
    getNextCommand();
}

void LevelScriptVM::cmdSetMusic() {
    getNextCommand();
}

void LevelScriptVM::cmdSetMenuMusic() {
    getNextCommand();
}

void LevelScriptVM::cmd38() {
    getNextCommand();
}

void LevelScriptVM::cmdSetMacroObjects() {
    // MACRO_OBJECTS(objList)：objList 是当前关卡段里的 s16 数组。
    // 每个条目 5 个 s16：w0 = (preset+0x1F) | (yaw<<9)，w1..3 = XYZ，
    // w4 = bhvParam；以 -1（或 presetID < 0）结束。
    // 只记录原始条目，preset → 模型 id 的解析留给 LevelExtract。
    if (current_area_index != -1) {
        const SegmentedAddress list_addr = segAddress(current_command.cmdGet<uint32_t>(4));
        auto data = seg_table.data(list_addr);
        size_t i = 0;
        while (i + 5 * 2 <= data.size()) {
            const int16_t w0 = readInt<int16_t>(data, i);
            if (w0 == -1 || ((w0 & 0x1FF) - 31) < 0) {
                break;
            }
            MacroObjectSpawnInfo info;
            info.preset = (w0 & 0x1FF) - 31;
            info.yaw = ((w0 >> 9) & 0x7F) << 9;
            info.pos.x = readInt<int16_t>(data, i + 2);
            info.pos.y = readInt<int16_t>(data, i + 4);
            info.pos.z = readInt<int16_t>(data, i + 6);
            info.bhv_param = readInt<int16_t>(data, i + 8);
            level.areas[current_area_index].macro_objects.push_back(info);
            i += 5 * 2;
        }
    }

    getNextCommand();
}

void LevelScriptVM::cmd3A() {
    getNextCommand();
}

void LevelScriptVM::cmdCreateWhirlpool() {
    getNextCommand();
}

void LevelScriptVM::cmdGetOrSetVar() {
    uint8_t op = current_command.cmdGet<uint8_t>(2);
    uint8_t var = current_command.cmdGet<uint8_t>(3);

    if (op == 0) { // OP_SET: write reg into the variable
        switch (var) {
            case 0: curr_save_file_num = reg; break;
            case 1: curr_course_num = reg; break;
            case 2: curr_act_num = reg; break;
            case 3: curr_level_num = reg; break;
            case 4: curr_area_index = reg; break;
        }
    } else { // OP_GET: read the variable into reg
        switch (var) {
            case 0: reg = curr_save_file_num; break;
            case 1: reg = curr_course_num; break;
            case 2: reg = curr_act_num; break;
            case 3: reg = curr_level_num; break;
            case 4: reg = curr_area_index; break;
        }
    }

    getNextCommand();
}
