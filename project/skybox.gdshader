shader_type sky;
// SM64 天空盒（decomp src/game/skybox.c）：不是 3D 盒子，而是按相机
// yaw/pitch 在屏幕上滚动的 10×8 图块贴图集（每图块 = 45°，2 图块 = 90°；
// fov 硬编码 90，正交窗口映射整屏）。逐像素复刻：
//   calculate_skybox_scaled_x/y + get_top_left_tile_idx + 3×3 网格。
// 像素的 N64 屏幕坐标 = SCREEN_UV（Godot 的 SCREEN_UV 使用左上角原点，
// 与 skybox.c 的正交窗口屏幕坐标一致）。

uniform sampler2D u_sky;            // 10×8 图块贴图集（320×256 RGBA8）
uniform vec4 u_mask = vec4(1.0);    // sSkyboxColors 遮罩（位与 → 乘法近似）
uniform float u_cam_yaw = 0.0;      // 相机 forward 的 SM64 yaw（弧度，atan2(x,z)）
uniform float u_cam_pitch = 0.0;    // 相机 forward 相对水平面的 pitch（弧度）
uniform vec4 u_clear = vec4(0.0, 0.0, 0.0, 1.0); // 网格外的清除色（游戏清屏色）

const float SCREEN_W = 320.0;
const float SCREEN_H = 240.0;
const float TILE_W = 160.0;         // SCREEN_W / 2
const float TILE_H = 120.0;         // SCREEN_H / 2
const float SKYBOX_W = 1280.0;      // 4 * SCREEN_W
const float SKYBOX_H = 960.0;       // 4 * SCREEN_H

vec3 sample_skybox(vec2 screen_px, float scaled_x, float scaled_y) {
	// skybox.c 的正交窗口：world x 从 scaledX 开始，world y 从 scaledY 向下。
	float world_x = scaled_x + screen_px.x;
	float world_y = scaled_y - screen_px.y;
	float ul_col = floor(scaled_x / TILE_W);
	float ul_row = floor((SKYBOX_H - scaled_y) / TILE_H);
	float tile_col = floor(world_x / TILE_W);
	float tile_row = floor((SKYBOX_H - world_y) / TILE_H);
	if (tile_col < ul_col || tile_col > ul_col + 2.0
			|| tile_row < ul_row || tile_row > ul_row + 2.0) {
		return u_clear.rgb;
	}

	// 图块内 UV：游戏的顶点 UV 跨 31 纹素（0..31<<5，无半纹素内缩）。
	// 以 skybox 图像坐标计算 t，避免在图块边界处把 t=0 误算成 t=1。
	float fu = fract(world_x / TILE_W);
	float fv = fract((SKYBOX_H - world_y) / TILE_H);
	vec2 uv = (vec2(tile_col, tile_row) * 32.0 + vec2(fu, fv) * 31.0)
			/ vec2(320.0, 256.0);
	vec4 c = texture(u_sky, uv);
	// sSkyboxColors：RDP 的 MODULATERGB；浮点乘法是其归一化近似。
	return c.rgb * u_mask.rgb;
}

void sky() {
	vec3 color;
	if (AT_CUBEMAP_PASS) {
		// Godot 仍使用 Sky 更新 radiance cubemaps。N64 没有 cubemap，
		// 但输出同一 tilemap 的方向采样，避免 sky-based ambient 变成黑色。
		vec3 dir = normalize(EYEDIR);
		float yaw = atan(dir.x, dir.z);
		float pitch = atan(dir.y, length(dir.xz));
		float yaw_u16 = mod(yaw / 6.28318530718 * 65536.0, 65536.0);
		float yaw_scaled = SCREEN_W * 360.0 * yaw_u16 / (90.0 * 65536.0);
		float scaled_xp = floor(yaw_scaled + 0.5);
		if (scaled_xp > SKYBOX_W) {
			scaled_xp -= SKYBOX_W;
		}
		float scaled_x = SKYBOX_W - scaled_xp;
		float pitch_u16 = pitch / 6.28318530718 * 65536.0;
		float pitch_deg = pitch_u16 * 360.0 / 65535.0;
		float scaled_y = floor(4.0 * pitch_deg + 0.5) + 5.0 * TILE_H;
		scaled_y = clamp(scaled_y, SCREEN_H, SKYBOX_H);
		color = sample_skybox(vec2(SCREEN_W * 0.5, SCREEN_H * 0.5), scaled_x, scaled_y);
	} else {
		// 像素的 N64 屏幕坐标：SCREEN_UV.y = 0 在屏幕顶部。图块行从上到下
		// 递增：行 0（亮/天空）在上，行 4-7（暗/海面）在下。
		float nx = SCREEN_UV.x * SCREEN_W;
		float ny = SCREEN_UV.y * SCREEN_H;

		// yaw：游戏的 atan2s 返回 u16（0..65535），负角回绕。
		float yaw = mod(u_cam_yaw / 6.28318530718 * 65536.0, 65536.0);
		float yaw_scaled = SCREEN_W * 360.0 * yaw / (90.0 * 65536.0);
		float scaled_xp = floor(yaw_scaled + 0.5);
		if (scaled_xp > SKYBOX_W) {
			scaled_xp -= SKYBOX_W;
		}
		float scaled_x = SKYBOX_W - scaled_xp;

		// pitch（u16）→ scaledY（钳制 [SCREEN_H, SKYBOX_H]）。u_cam_pitch 是相对
		// 水平面的有符号角度，正值向上，正是 skybox.c 的 pitch 约定。
		float pitch = u_cam_pitch / 6.28318530718 * 65536.0;
		float pitch_deg = pitch * 360.0 / 65535.0;
		float scaled_y = floor(4.0 * pitch_deg + 0.5) + 5.0 * TILE_H;
		scaled_y = clamp(scaled_y, SCREEN_H, SKYBOX_H);

		color = sample_skybox(vec2(nx, ny), scaled_x, scaled_y);
	}
	COLOR = color;
}
