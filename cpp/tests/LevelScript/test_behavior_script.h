#ifndef TEST_BEHAVIOR_SCRIPT_H
#define TEST_BEHAVIOR_SCRIPT_H

// 走查对象行为脚本（段 0x13）并校验静态解释器提取的数据：
// 原版与 Treasure World 的门行为（bhvDoor）都带 LOAD_ANIMATIONS(门动画) +
// ANIMATE(0) + LOAD_COLLISION_DATA + SET_HITBOX(80,100)；测试在段 0x13 里按
// 命令形状扫描定位它（两版地址不同，不能写死），再校验解释结果。同时对所有
// 关卡对象的行为脚本做健壮性走查（不崩溃、空/越界脚本返回 !ok）。
void testBehaviorScript();

#endif /* TEST_BEHAVIOR_SCRIPT_H */
