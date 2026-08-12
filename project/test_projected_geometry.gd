extends SceneTree

func _init() -> void:
	var main_scene := preload("res://main.tscn").instantiate()
	root.add_child(main_scene)
	await process_frame

	var bridge = ClassDB.instantiate("GodotBridge")
	if bridge == null:
		push_error("GodotBridge is unavailable")
		quit(1)
		return
	bridge.loadROM(ProjectSettings.globalize_path("res://../cpp/baserom.us.z64"))
	if not bridge.ROMLoaded() or not bridge.extractLevel(9, 1):
		push_error("BOB extraction failed")
		quit(1)
		return

	var meshes: Array = bridge.getMeshes()
	var materials: Array = bridge.getMaterials()
	var projection_context: Dictionary = bridge.getProjectionContext()
	if projection_context.is_empty() or not projection_context.get("perspective", false) \
			or float(projection_context.get("fov", 0.0)) <= 0.0 \
			or float(projection_context.get("far", 0.0)) <= float(projection_context.get("near", 0.0)) \
			or projection_context.get("view", PackedFloat32Array()).size() != 16 \
			or projection_context.get("projection", PackedFloat32Array()).size() != 16:
		push_error("BOB has no valid extracted perspective context")
		quit(1)
		return
	var projected := 0
	for mesh in meshes:
		if not mesh.get("rsp_projected", false):
			continue
		projected += 1
		var vertex_count: int = mesh.vertices.size()
		if mesh.rsp_ndc.size() != vertex_count * 4 \
				or mesh.rsp_viewport.size() != vertex_count * 3 \
				or mesh.rsp_depth.size() != vertex_count \
				or mesh.rsp_clip_codes.size() != vertex_count:
			push_error("projected vertex arrays are not aligned")
			quit(1)
			return

	if projected == 0:
		push_error("BOB area has no projected static geometry")
		quit(1)
		return
	var static_dynamic_lit_materials := 0
	for mesh in meshes:
		var material_data: Dictionary = materials[int(mesh.material)]
		if not material_data.get("lit", false) or not material_data.get("use_vertex", false) \
				or int(material_data.get("num_lights", 0)) == 0:
			continue
		var material: Material = main_scene._build_material(material_data, int(mesh.layer))
		if not material is ShaderMaterial \
				or not material.get_shader_parameter("use_dynamic_lighting"):
			push_error("static lit geometry did not use dynamic model-view lighting")
			quit(1)
			return
		static_dynamic_lit_materials += 1
	if static_dynamic_lit_materials == 0:
		push_error("BOB has no static lit material to validate")
		quit(1)
		return
	var inline_objects: Array = bridge.getInlineObjectModels()
	var objects: Array = bridge.getObjects()
	if inline_objects.is_empty():
		push_error("BOB has no inline object geometry")
		quit(1)
		return
	var billboard_models: Array = []
	billboard_models.append_array(inline_objects)
	billboard_models.append_array(bridge.getObjectModels())
	var projected_billboard_parts := 0
	for model_md in billboard_models:
		for part in model_md.get("billboard_parts", []):
			for material_data in part.materials:
				if material_data.get("force_projection", false):
					projected_billboard_parts += 1
	if projected_billboard_parts == 0:
		push_error("BOB billboard parts did not carry projection state")
		quit(1)
		return
	var inline_projected := 0
	for inline_md in inline_objects:
		for mesh in inline_md.meshes:
			if not mesh.get("rsp_projected", false):
				continue
			inline_projected += 1
			if mesh.rsp_ndc.size() != mesh.vertices.size() * 4:
				push_error("inline projected vertex arrays are not aligned")
				quit(1)
				return
	if inline_projected == 0:
		push_error("BOB inline objects have no projected geometry")
		quit(1)
		return
	var dynamic_lit_materials := 0
	for inline_md in inline_objects:
		for material_data in inline_md.materials:
			if not material_data.get("lit", false) or not material_data.get("use_vertex", false) \
					or int(material_data.get("num_lights", 0)) == 0:
				continue
			dynamic_lit_materials += 1
			var material: Material = main_scene._build_material(material_data, 0)
			if not material is ShaderMaterial \
					or not material.get_shader_parameter("use_dynamic_lighting"):
				push_error("inline lit flat material did not use dynamic lighting")
				quit(1)
				return
	if dynamic_lit_materials == 0:
		push_error("BOB has no inline lit flat material to validate")
		quit(1)
		return

	main_scene.rom_manager = bridge
	main_scene._apply_projection_context()
	var billboard_material_data: Dictionary = {}
	for model_md in billboard_models:
		for part in model_md.get("billboard_parts", []):
			for material_data in part.materials:
				if material_data.get("force_projection", false):
					billboard_material_data = material_data
					break
			if not billboard_material_data.is_empty():
				break
		if not billboard_material_data.is_empty():
			break
	var billboard_material: Material = main_scene._build_material(billboard_material_data, 4)
	if not billboard_material is ShaderMaterial \
			or not billboard_material.get_shader_parameter("use_rsp_projection") \
			or not billboard_material.get_shader_parameter("alpha_scissor"):
		push_error("layer-4 billboard did not use projected alpha-scissor material")
		quit(1)
		return
	var projected_alpha_layers := 0
	for mesh in meshes:
		var layer := int(mesh.layer)
		if layer < 4:
			continue
		var material_data: Dictionary = materials[int(mesh.material)]
		var alpha_material: Material = main_scene._build_material(material_data, layer)
		if not material_data.get("rsp_projection", false) \
				or not alpha_material is ShaderMaterial \
				or not alpha_material.get_shader_parameter("use_rsp_projection"):
			push_error("alpha layer did not use the extracted projection")
			quit(1)
			return
		if layer == 4:
			if not alpha_material.get_shader_parameter("alpha_scissor"):
				push_error("layer-4 alpha material did not use alpha scissor")
				quit(1)
				return
		else:
			if alpha_material.shader != main_scene._sm64_projected_alpha_shader:
				push_error("layers 5-7 did not use the projected blend shader")
				quit(1)
				return
		projected_alpha_layers += 1
	if projected_alpha_layers == 0:
		push_error("BOB has no projected alpha layer to validate")
		quit(1)
		return
	var tree_materials_checked := 0
	for inline_md in inline_objects:
		var object_index := int(inline_md.object)
		if object_index < 0 or object_index >= objects.size() \
				or int(objects[object_index].model) != 0x17:
			continue
		for mesh in inline_md.meshes:
			var tree_material: Dictionary = inline_md.materials[int(mesh.material)]
			var tree_shader: Material = main_scene._build_material(tree_material, int(mesh.layer))
			var expected_clamp := Vector4(
				0.5 / float(tree_material.tex_width),
				0.5 / float(tree_material.tex_height),
				1.0 - 0.5 / float(tree_material.tex_width),
				1.0 - 0.5 / float(tree_material.tex_height))
			if tree_material.get("repeat_s", true) \
					or tree_material.get("repeat_t", true) \
					or not tree_shader is ShaderMaterial \
					or not tree_shader.get_shader_parameter("clamp_uv") \
					or not tree_shader.get_shader_parameter("uv_clamp").is_equal_approx(expected_clamp):
				push_error("BOB tree projected texture clamp rectangle is incorrect")
				quit(1)
				return
			tree_materials_checked += 1
	if tree_materials_checked == 0:
		push_error("BOB has no inline tree material to validate")
		quit(1)
		return
	var goomba_face_materials_checked := 0
	for inline_md in inline_objects:
		var object_index := int(inline_md.object)
		if object_index < 0 or object_index >= objects.size() \
				or int(objects[object_index].model) != 0xC0:
			continue
		for part in inline_md.get("billboard_parts", []):
			for mesh in part.meshes:
				var face_material: Dictionary = part.materials[int(mesh.material)]
				if not face_material.get("textured", false):
					continue
				var uv_max_x := -INF
				for uv in mesh.uvs:
					uv_max_x = maxf(uv_max_x, uv.x)
				if face_material.get("tile_mask_s", -1) != 5 \
						or face_material.get("tile_mask_t", -1) != 5 \
						or not face_material.get("repeat_s", false) \
						or not face_material.get("repeat_t", false) \
						or uv_max_x < 0.98 or uv_max_x > 0.985:
					push_error("Goomba face does not use RDP texel-center UVs")
					quit(1)
					return
				goomba_face_materials_checked += 1
	if goomba_face_materials_checked == 0:
		push_error("BOB has no Goomba face billboard material to validate")
		quit(1)
		return
	if not _check_castle_boo_clamp(bridge):
		quit(1)
		return
	if not bridge.extractLevel(9, 1):
		push_error("BOB re-extraction failed after castle texture check")
		quit(1)
		return
	main_scene._place_camera()
	var expected_camera: Transform3D = main_scene._camera_transform_from_view(projection_context.view)
	if not main_scene.camera.global_position.is_equal_approx(expected_camera.origin) \
			or not main_scene.camera.global_basis.is_equal_approx(expected_camera.basis):
		push_error("Godot camera did not adopt the extracted geo camera pose")
		quit(1)
		return
	if not is_equal_approx(main_scene.camera.fov, float(projection_context.fov)) \
			or not is_equal_approx(main_scene.camera.near, float(projection_context.near)) \
			or not is_equal_approx(main_scene.camera.far, float(projection_context.far)):
		push_error("Godot camera did not adopt the extracted perspective context")
		quit(1)
		return
	main_scene._render_geometry()
	await process_frame
	await process_frame
	if main_scene.model_root.get_child_count() == 0:
		push_error("projected geometry was not rendered")
		quit(1)
		return
	var first_mesh: MeshInstance3D = main_scene.model_root.get_child(0)
	if not first_mesh.material_override is ShaderMaterial:
		push_error("projected area geometry did not use a ShaderMaterial")
		quit(1)
		return
	var projected_material: ShaderMaterial = first_mesh.material_override
	if projected_material.get_shader_parameter("use_rsp_position"):
		push_error("projected geometry incorrectly bypasses the moving camera")
		quit(1)
		return
	if not projected_material.get_shader_parameter("use_dynamic_reprojection"):
		push_error("projected geometry did not enable dynamic camera reprojection")
		quit(1)
		return
	if not projected_material.get_shader_parameter("use_rsp_projection"):
		push_error("projected geometry did not use the extracted projection matrix")
		quit(1)
		return
	if main_scene._billboard_nodes.is_empty():
		push_error("BOB has no camera-dependent billboard nodes")
		quit(1)
		return
	var billboard_node: Node3D = main_scene._billboard_nodes[0]
	var billboard_basis_before: Basis = billboard_node.global_basis
	var billboard_positions_before: Array[Vector3] = []
	for node in main_scene._billboard_nodes:
		billboard_positions_before.append(node.global_position)
	main_scene.camera.rotation.y += 0.25
	main_scene._process(0.0)
	var billboard_basis_after: Basis = billboard_node.global_basis
	main_scene.camera.rotation.y -= 0.25
	if billboard_basis_before.x.is_equal_approx(billboard_basis_after.x) \
			and billboard_basis_before.z.is_equal_approx(billboard_basis_after.z):
		push_error("billboard basis did not follow the camera")
		quit(1)
		return
	for i in main_scene._billboard_nodes.size():
		if not billboard_positions_before[i].is_equal_approx(
				main_scene._billboard_nodes[i].global_position):
			push_error("billboard anchor moved with the camera")
			quit(1)
			return
	if not await _check_dynamic_reprojection(main_scene):
		quit(1)
		return
	print("projected geometry: meshes=", meshes.size(), " projected=", projected,
			" static_dynamic_lit=", static_dynamic_lit_materials,
			" inline=", inline_objects.size(), " inline_projected=", inline_projected)
	quit()

func _check_castle_boo_clamp(bridge) -> bool:
	if not bridge.extractLevel(6, 1):
		push_error("castle extraction failed for Boo texture check")
		return false
	var objects: Array = bridge.getObjects()
	var boo_materials_checked := 0
	var boo_eye_atlas_checked := 0
	for inline_md in bridge.getInlineObjectModels():
		var object_index := int(inline_md.object)
		if object_index < 0 or object_index >= objects.size() \
				or int(objects[object_index].model) != 0x65:
			continue
		for mesh in inline_md.meshes:
			var material_data: Dictionary = inline_md.materials[int(mesh.material)]
			if not material_data.get("textured", false):
				continue
			var expected_clamp := Vector4(
				0.5 / float(material_data.tex_width),
				0.5 / float(material_data.tex_height),
				1.0 - 0.5 / float(material_data.tex_width),
				1.0 - 0.5 / float(material_data.tex_height))
			if material_data.get("repeat_s", true) \
					or material_data.get("repeat_t", true) \
					or not material_data.get("uv_clamp", Vector4(-1, -1, -1, -1)).is_equal_approx(expected_clamp):
				push_error("castle Boo texture is still configured to repeat")
				return false
			boo_materials_checked += 1
			if int(material_data.get("tex_width", 0)) == 64 \
					and int(material_data.get("tex_height", 0)) == 32:
				boo_eye_atlas_checked += 1
	if boo_materials_checked == 0 or boo_eye_atlas_checked == 0:
		push_error("castle Boo eye atlas material was not extracted")
		return false
	return true

func _visible_bounds(image: Image) -> Vector2:
	var min_x := image.get_width()
	var max_x := -1
	for y in image.get_height():
		for x in image.get_width():
			if image.get_pixel(x, y).r > 0.1:
				min_x = mini(min_x, x)
				max_x = maxi(max_x, x)
	if max_x < min_x:
		return Vector2(-1, -1)
	return Vector2(min_x, max_x)

func _check_dynamic_reprojection(main_scene: Node) -> bool:
	if DisplayServer.get_name() == "headless":
		print("projected pixel check skipped: headless renderer has no framebuffer")
		return true
	var viewport := SubViewport.new()
	viewport.size = Vector2i(128, 128)
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var world := Node3D.new()
	viewport.add_child(world)
	var camera := Camera3D.new()
	camera.position = Vector3(0, 0, 5)
	camera.current = true
	world.add_child(camera)

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([
		Vector3(-1, -1, 0), Vector3(1, -1, 0), Vector3(0, 1, 0)])
	arrays[Mesh.ARRAY_NORMAL] = PackedVector3Array([
		Vector3(0, 0, 1), Vector3(0, 0, 1), Vector3(0, 0, 1)])
	arrays[Mesh.ARRAY_TEX_UV] = PackedVector2Array([
		Vector2.ZERO, Vector2.ONE, Vector2(0.5, 1)])
	arrays[Mesh.ARRAY_TANGENT] = PackedFloat32Array([
		0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1])
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2])
	var array_mesh := ArrayMesh.new()
	array_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var instance := MeshInstance3D.new()
	instance.mesh = array_mesh
	var material_data := {
		"textured": false,
		"color_source": 3,
		"color": Color.WHITE,
		"env_color": Color.WHITE,
		"use_vertex": false,
		"repeat_s": false,
		"repeat_t": false,
	}
	instance.material_override = main_scene._build_projected_material(material_data, null)
	world.add_child(instance)
	await process_frame
	await process_frame
	var before_image: Image = viewport.get_texture().get_image()
	if before_image == null:
		print("projected pixel check skipped: headless renderer has no framebuffer")
		viewport.queue_free()
		return true
	var before := _visible_bounds(before_image)
	camera.position.x = 1.0
	await process_frame
	await process_frame
	var after := _visible_bounds(viewport.get_texture().get_image())
	viewport.queue_free()
	if before.x < 0 or after.x < 0:
		push_error("dynamic projected triangle was not visible")
		return false
	if is_equal_approx(before.x, after.x) and is_equal_approx(before.y, after.y):
		push_error("dynamic projected triangle did not move with the camera")
		return false
	return true
