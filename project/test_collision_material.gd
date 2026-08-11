extends SceneTree

const MainScript = preload("res://main.gd")

func _init() -> void:
	var main := MainScript.new()
	var material: StandardMaterial3D = main._build_collision_material()
	if material.shading_mode != BaseMaterial3D.SHADING_MODE_UNSHADED:
		push_error("collision material must be unshaded")
		material = null
		main.free()
		quit(1)
		return
	if not material.vertex_color_use_as_albedo:
		push_error("collision material must use vertex colors")
		material = null
		main.free()
		quit(1)
		return
	if material.cull_mode != BaseMaterial3D.CULL_DISABLED:
		push_error("collision material must be double-sided")
		material = null
		main.free()
		quit(1)
		return
	material = null
	main.free()
	quit()
