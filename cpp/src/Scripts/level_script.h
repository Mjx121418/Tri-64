#ifndef LEVEL_SCRIPT_H
#define LEVEL_SCRIPT_H

#include "Level/area.h"
#include "Log.h"
#include "Memory/segment.h"
#include "Scripts/geo_layout.h"
#include <cstdint>
#include <span>

enum class LevelCommandError {
    InvalidRomOffset,
    InvalidSegment,
    MIO0Error
};

struct LevelCommand {
    SegmentedAddress addr;

    uint8_t opcode;
    uint8_t length;

    std::span<const uint8_t> data;

    template <std::integral T>
    T cmdGet(int offset) {
        return readInt<T>(data, offset);
    }
};

class LevelScriptVM {
    Level &level;
    GeoLayoutProcessor geo_layout_processor;
    SegmentedAddress pc; // segmented
    uint32_t reg;
    std::vector<LevelCommand> command_stack;
    std::vector<uint32_t> frame_stack;
    std::vector<int16_t> argument_stack;
    SegmentTable &seg_table;
    LevelCommand current_command;
    WarningLog &warnings_;

    // Game state, mirrors the relevant decomp globals (gCurrSaveFileNum,
    // gCurrCourseNum, gCurrActNum, gCurrLevelNum, gCurrAreaIndex).
    int32_t curr_save_file_num { 0 };
    int32_t curr_course_num { 0 };
    int32_t curr_act_num { 1 };
    int32_t curr_level_num { 0 };
    int32_t curr_area_index { -1 };

    // "All acts" 模式：忽略 OBJECT_WITH_ACTS 的 act 位掩码（每个 OBJECT 命令
    // 只出现一次，去掉门禁即得到所有 act 的对象，无需多次运行脚本再合并）。
    bool ignore_acts_ { false };

    // occured in "level_script.c", not necessarily useful.
    int16_t current_area_index { -1 };
    int16_t script_status { 1 };

    void ExecuteCommand();
    void getNextCommand();

    // This function is copied from "level_script.c".
    int32_t evalScriptOp(int8_t op, int32_t arg);

    // 0x00
    void cmdLoadAndExcute();
    /*01*/
    void cmdExitAndExecute();
    /*02*/
    void cmdExit();
    /*03*/
    void cmdSleep();
    /*04*/
    void cmdSleep2();
    // 0x05
    void cmdJump();
    // 0x06
    void cmdJumpAndLink();
    /*07*/
    void cmdReturn();
    /*08*/
    void cmdJumpAndLinkPushArg();
    /*09*/
    void cmdJumpRepeat();
    /*0A*/
    void cmdLoopBegin();
    // 0x0B
    void cmdLoopUntil();
    // 0x0C
    void cmdJumpIf();
    // 0x0D
    void cmdJumpAndLinkIf();
    // 0x0E
    void cmdSkipIf();
    /*0F*/
    void cmdSkip();
    /*10*/
    void cmdSkippableNOP();
    /*11*/
    void cmdCall();
    /*12*/
    void cmdCallLoop();
    /*13*/
    void cmdSetRegister();
    /*14*/
    void cmdPushPoolState();
    /*15*/
    void cmdPopPoolState();
    /*16*/
    void cmdLoadToFixedAddress();
    // 0x17
    void cmdLoadRaw();
    // 0x18
    void cmdLoadMIO0();
    /*19*/
    void cmdLoadMarioHead();
    /*1A*/
    void cmdLoadMIO0Texture();
    /*1B*/
    void cmdInitLevel();
    /*1C*/
    void cmdClearLevel();
    /*1D*/
    void cmdAllocLevelPool();
    /*1E*/
    void cmdFreeLevelPool();
    // 0x1F
    void cmdBeginArea();
    // 0x20
    void cmdEndArea();
    // 0x21
    void cmdLoadModelFromDL();
    // 0x22
    void cmdLoadModelFromGeo();
    // 0x23
    void cmd23();
    // 0x24
    void cmdPlaceObject();
    // 0x25
    void cmdInitMario();
    // 0x26
    void cmdCreateWarpNode();
    // 0x27
    void cmdCreatePaintingWarpNode();
    // 0x28
    void cmdCreateInstantWarp();
    /*29*/
    void cmdLoadArea();
    /*2A*/
    void cmdUnloadArea();
    /*2B*/
    void cmdSetMarioStartPos();
    /*2C*/
    void cmd2C();
    /*2D*/
    void cmd2D();
    /*2E*/
    void cmdSetTerrainData();
    /*2F*/
    void cmdSetRooms();
    /*30*/
    void cmdShowDialog();
    /*31*/
    void cmdSetTerrainType();
    /*32*/
    void cmdNOP();
    /*33*/
    void cmdSetTransition();
    /*34*/
    void cmdSetBlackout();
    /*35*/
    void cmdSetGamma();
    /*36*/
    void cmdSetMusic();
    /*37*/
    void cmdSetMenuMusic();
    /*38*/
    void cmd38();
    /*39*/
    void cmdSetMacroObjects();
    /*3A*/
    void cmd3A();
    /*3B*/
    void cmdCreateWhirlpool();
    /*3C*/
    void cmdGetOrSetVar();

public:
    LevelScriptVM(SegmentTable &seg_table, Level &level, WarningLog &warnings);

    // Runs the script at `entry` until it terminates. For a static export the
    // level is considered loaded as soon as the script would pause at
    // CALL_LOOP (the point where the game starts rendering the level).
    void execute(SegmentedAddress entry);

    void setLevelNum(int32_t level_num) { curr_level_num = level_num; }
    void setActNum(int32_t act_num) { curr_act_num = act_num; }
    // 0 = 所有 act 的对象都生成（忽略 OBJECT_WITH_ACTS 的掩码）。
    void setIgnoreActs(bool ignore) { ignore_acts_ = ignore; }
    int32_t getCourseNum() const { return curr_course_num; }
    int32_t getLevelNum() const { return curr_level_num; }
};

#endif /* LEVEL_SCRIPT_H */
