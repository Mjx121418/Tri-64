// Projected alpha path for Fast3D layers 5-7. These layers blend and do not
// update the depth buffer, unlike layer 4's opaque alpha scissor path.
shader_type spatial;
render_mode unshaded, cull_disabled, blend_mix, depth_draw_never;

uniform sampler2D albedo_tex : source_color, repeat_disable;
uniform bool use_texel = false;
uniform bool use_vertex = false;
uniform vec3 base_color = vec3(1.0);
uniform bool repeat_s = true;
uniform bool repeat_t = true;
uniform bool clamp_uv = false;
uniform vec4 uv_clamp = vec4(0.0, 0.0, 1.0, 1.0);
uniform float material_alpha = 1.0;
// Extraction-time NDC is only valid while the matching camera is fixed.
uniform bool use_rsp_position = false;
uniform bool use_dynamic_reprojection = true;
uniform bool use_rsp_projection = false;
uniform mat4 rsp_projection = mat4(1.0);
uniform bool use_dynamic_lighting = false;
uniform int num_lights = 0;
uniform vec3 ambient = vec3(0.0);
uniform vec3 light_dirs[8];
uniform vec3 light_cols[8];
uniform bool cull_back = true;

varying vec3 vertex_color;
varying float vertex_alpha;
varying vec3 lighting_shade;

void vertex() {
	if (use_rsp_position) {
		POSITION = vec4(TANGENT.xyz, 1.0);
	} else if (use_dynamic_reprojection) {
		vec4 view_position = MODELVIEW_MATRIX * vec4(VERTEX, 1.0);
		vec4 clip_position = (use_rsp_projection ? rsp_projection : PROJECTION_MATRIX) * view_position;
		if (use_rsp_projection) {
			clip_position.y = -clip_position.y;
			if (CLIP_SPACE_FAR == 0.0) {
				clip_position.z = (clip_position.w - clip_position.z) * 0.5;
			}
		}
		POSITION = clip_position;
	}
	if (use_dynamic_lighting) {
		vec3 n = normalize(NORMAL);
		mat4 inverse_modelview = inverse(MODELVIEW_MATRIX);
		vec3 shade = ambient;
		for (int i = 0; i < 8; i++) {
			if (i < num_lights) {
				vec3 light = normalize((inverse_modelview * vec4(light_dirs[i], 0.0)).xyz);
				shade += max(dot(n, light), 0.0) * light_cols[i];
			}
		}
		lighting_shade = clamp(shade, 0.0, 1.0);
	} else {
		lighting_shade = vec3(1.0);
	}
	vertex_color = use_vertex ? COLOR.rgb : vec3(1.0);
	vertex_alpha = use_vertex ? COLOR.a : 1.0;
}

void fragment() {
	if (cull_back && FRONT_FACING) {
		discard;
	}
	vec2 uv = UV;
	vec2 clamp_min = clamp_uv ? uv_clamp.xy : vec2(0.0);
	vec2 clamp_max = clamp_uv ? uv_clamp.zw : vec2(1.0);
	uv.x = repeat_s ? fract(uv.x) : clamp(uv.x, clamp_min.x, clamp_max.x);
	uv.y = repeat_t ? fract(uv.y) : clamp(uv.y, clamp_min.y, clamp_max.y);
	vec4 texel = use_texel ? texture(albedo_tex, uv) : vec4(1.0);
	float alpha = texel.a * material_alpha * vertex_alpha;
	vec3 base = use_texel ? texel.rgb :
			(use_dynamic_lighting && use_vertex ? vec3(1.0) : base_color);
	ALBEDO = base * (use_dynamic_lighting ? lighting_shade : vertex_color);
	ALPHA = alpha;
}
