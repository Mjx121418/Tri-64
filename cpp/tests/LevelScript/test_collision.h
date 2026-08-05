#ifndef TEST_COLLISION_H
#define TEST_COLLISION_H

// 解码关卡碰撞数据（表面/顶点/房间/水盒/特殊对象）并校验数量。
// 原版 BOB（关卡 9，区域 1）有已知计数：570 顶点 / 1060 表面 / 17 特殊对象；
// HMC（关卡 7，区域 1）带 ROOMS 数据。
void testCollision();

#endif /* TEST_COLLISION_H */
