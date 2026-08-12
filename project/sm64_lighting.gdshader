// SM64 逐顶点光照（fast3d 的 ambient + Σ max(0, n̂·l̂)·color）。
// 原始 Light_t 方向在 vertex 阶段按 Fast3D 的 model-view 矩阵变换到网格空间。
shader_type spatial;
render_mode unshaded, cull_disabled;

uniform sampler2D albedo_tex : source_color, repeat_disable; // sRGB→linear，与 StandardMaterial3D 的
                                             // albedo_texture 一致（否则纹理会偏亮）
uniform bool use_texel = false;      // 纹理采样（G_CC_MODULATERGB 等）
uniform bool repeat_s = true;        // S 方向 G_TX_WRAP/MIRROR
uniform bool repeat_t = true;        // T 方向 G_TX_WRAP/MIRROR
uniform bool clamp_uv = false;       // 两轴 CLAMP（否则 WRAP）
uniform vec4 uv_clamp = vec4(0.0, 0.0, 1.0, 1.0); // clamp 区域的归一化 UV
uniform int num_lights = 0;          // 方向光数量（0 = 不调光，shade = 白）
uniform vec3 ambient = vec3(0.0);    // 环境光（槽 num_lights）
uniform vec3 light_dirs[8];          // 原始 Light_t 方向（RSP 光照空间）
uniform vec3 light_cols[8];          // 方向光颜色
uniform bool use_vertex_shade = false; // exact Fast3D SHADE supplied as COLOR
uniform bool cull_back = true;       // SM64 默认 G_CULL_BACK

varying vec3 vertex_shade;

void vertex() {
	if (use_vertex_shade) {
		vertex_shade = COLOR.rgb;
	} else {
		// Fast3D keeps the vertex normal in local coordinates and inverse-transforms
		// each raw Light_t direction by the active model-view matrix before the dot.
		// Do the same here instead of treating the raw DL direction as world-space.
		vec3 n = normalize(NORMAL);
		mat4 inverse_modelview = inverse(MODELVIEW_MATRIX);
		vec3 shade = ambient;
		for (int i = 0; i < 8; i++) {
			if (i < num_lights) {
				vec3 light = normalize((inverse_modelview * vec4(light_dirs[i], 0.0)).xyz);
				shade += max(dot(n, light), 0.0) * light_cols[i];
			}
		}
		vertex_shade = clamp(shade, 0.0, 1.0);
	}
}

void fragment() {
	// Godot's screen-space winding is opposite the imported Fast3D winding.
	if (cull_back && FRONT_FACING) {
		discard;
	}
	vec2 uv = UV;
	vec2 clamp_min = clamp_uv ? uv_clamp.xy : vec2(0.0);
	vec2 clamp_max = clamp_uv ? uv_clamp.zw : vec2(1.0);
	uv.x = repeat_s ? fract(uv.x) : clamp(uv.x, clamp_min.x, clamp_max.x);
	uv.y = repeat_t ? fract(uv.y) : clamp(uv.y, clamp_min.y, clamp_max.y);
	vec3 base = use_texel ? texture(albedo_tex, uv).rgb : vec3(1.0);
	ALBEDO = base * vertex_shade;
}
