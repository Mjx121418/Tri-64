#ifndef TEST_COLLISION_H
#define TEST_COLLISION_H

// 解码关卡碰撞数据（表面/顶点/房间/水盒/特殊对象）并校验数量。
// 原版 BOB（关卡 9，区域 1）有已知计数：570 顶点 / 1060 表面 / 17 特殊对象；
// HMC（关卡 7，区域 1）带 ROOMS 数据。
void testCollision();

// 逐关卡提取 SMTW Dream Edition 宏（存在时），验证不会崩溃（越界的 geo /
// 脚本地址应被跳过或转为错误，而不是抛异常终止）。每关卡 area 1。
void testHackRobustness();

#endif /* TEST_COLLISION_H */
