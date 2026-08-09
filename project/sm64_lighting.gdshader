// SM64 逐顶点光照（fast3d 的 ambient + Σ max(0, n̂·l̂)·color），世界空间。
// 受光材质（G_LIGHTING）用它替代烘焙的顶点 shade，从而支持对象实例旋转：
// 法线在 vertex 阶段用 MODEL_MATRIX 转到世界（含节点旋转/缩放 → 实例正确），
// 与 C++ 端按世界空间导出的灯光点乘。
shader_type spatial;
render_mode unshaded, cull_disabled;

uniform sampler2D albedo_tex : source_color; // sRGB→linear，与 StandardMaterial3D 的
                                            // albedo_texture 一致（否则纹理会偏亮）
uniform bool use_texel = false;      // 纹理采样（G_CC_MODULATERGB 等）
uniform bool clamp_uv = false;       // 两轴 CLAMP（否则 WRAP）
uniform vec4 uv_clamp = vec4(0.0, 0.0, 1.0, 1.0); // clamp 区域的归一化 UV
uniform int num_lights = 0;          // 方向光数量（0 = 不调光，shade = 白）
uniform vec3 ambient = vec3(0.0);    // 环境光（槽 num_lights）
uniform vec3 light_dirs[8];          // 方向光方向（世界空间）
uniform vec3 light_cols[8];          // 方向光颜色

varying vec3 vertex_shade;

void vertex() {
	// Fast3D computes lighting when each vertex is loaded, then the RDP
	// interpolates that shade across the triangle. Per-fragment normal lighting
	// makes large triangles differ from SM64.
	vec3 n = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec3 shade = ambient;
	for (int i = 0; i < 8; i++) {
		if (i < num_lights) {
			shade += max(dot(n, normalize(light_dirs[i])), 0.0) * light_cols[i];
		}
	}
	vertex_shade = clamp(shade, 0.0, 1.0);
}

void fragment() {
	vec2 uv = UV;
	if (clamp_uv) {
		uv = clamp(UV, uv_clamp.xy, uv_clamp.zw);
	}
	vec3 base = use_texel ? texture(albedo_tex, uv).rgb : vec3(1.0);
	ALBEDO = base * vertex_shade;
}
