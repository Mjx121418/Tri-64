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
	var inline_objects: Array = bridge.getInlineObjectModels()
	if inline_objects.is_empty():
		push_error("BOB has no inline object geometry")
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
	if main_scene._billboard_nodes.is_empty():
		push_error("BOB has no camera-dependent billboard nodes")
		quit(1)
		return
	var billboard_node: Node3D = main_scene._billboard_nodes[0]
	var billboard_basis_before: Basis = billboard_node.global_basis
	main_scene.camera.rotation.y += 0.25
	main_scene._process(0.0)
	var billboard_basis_after: Basis = billboard_node.global_basis
	main_scene.camera.rotation.y -= 0.25
	if billboard_basis_before.x.is_equal_approx(billboard_basis_after.x) \
			and billboard_basis_before.z.is_equal_approx(billboard_basis_after.z):
		push_error("billboard basis did not follow the camera")
		quit(1)
		return
	if not await _check_dynamic_reprojection(main_scene):
		quit(1)
		return
	print("projected geometry: meshes=", meshes.size(), " projected=", projected,
			" inline=", inline_objects.size(), " inline_projected=", inline_projected)
	quit()

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
