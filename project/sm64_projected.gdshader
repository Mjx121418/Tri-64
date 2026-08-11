// Opaque static geometry path carrying Fast3D projection data and supporting
// live reprojection through the current Godot camera.
// TANGENT carries four floats per vertex; xyz are fixed-point NDC values and
// w is reserved so the channel remains a regular Godot tangent attribute.
shader_type spatial;
render_mode unshaded, cull_disabled;

uniform sampler2D albedo_tex : source_color;
uniform bool use_texel = false;
uniform bool use_vertex = false;
uniform vec3 base_color = vec3(1.0);
uniform bool clamp_uv = false;
uniform vec4 uv_clamp = vec4(0.0, 0.0, 1.0, 1.0);
// Extraction-time NDC is only valid while the matching camera is fixed.
uniform bool use_rsp_position = false;
uniform bool use_dynamic_reprojection = true;
// Inline lit objects use the same Fast3D light-space path as cached objects. Their
// fixed SHADE is still exported for inspection, but should not darken them twice.
uniform bool use_dynamic_lighting = false;
uniform int num_lights = 0;
uniform vec3 ambient = vec3(0.0);
uniform vec3 light_dirs[8]; // raw Light_t directions in RSP light space
uniform vec3 light_cols[8];
uniform bool cull_back = true;

varying vec3 vertex_color;
varying vec3 lighting_shade;

void vertex() {
	if (use_rsp_position) {
		POSITION = vec4(TANGENT.xyz, 1.0);
	} else if (use_dynamic_reprojection) {
		// Rebuild clip coordinates from the current camera every frame. This is
		// intentionally separate from the extraction-time fixed NDC override.
		POSITION = PROJECTION_MATRIX * MODELVIEW_MATRIX * vec4(VERTEX, 1.0);
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
}

void fragment() {
	// Godot's screen-space winding is opposite the imported Fast3D winding.
	if (cull_back && FRONT_FACING) {
		discard;
	}
	vec2 uv = UV;
	if (clamp_uv) {
		uv = clamp(UV, uv_clamp.xy, uv_clamp.zw);
	}
	vec3 base = use_texel ? texture(albedo_tex, uv).rgb :
			(use_dynamic_lighting && use_vertex ? vec3(1.0) : base_color);
	ALBEDO = base * (use_dynamic_lighting ? lighting_shade : vertex_color);
}
