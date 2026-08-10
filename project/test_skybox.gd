extends SceneTree

const MainScript = preload("res://main.gd")

func _init() -> void:
	_check_angles(Vector3(0, 0, 1), 0.0, 0.0, "+Z horizon")
	_check_angles(Vector3(1, 0, 0), PI * 0.5, 0.0, "+X horizon")
	_check_angles(Vector3(0, 1, 1), 0.0, PI * 0.25, "upward 45 degrees")
	_check_angles(Vector3(0, -1, 1), 0.0, -PI * 0.25, "downward 45 degrees")
	quit()

func _check_angles(forward: Vector3, expected_yaw: float, expected_pitch: float,
		label: String) -> void:
	var angles: Vector2 = MainScript._skybox_camera_angles(forward)
	if not is_equal_approx(angles.x, expected_yaw) or not is_equal_approx(angles.y, expected_pitch):
		push_error("skybox angle mismatch for %s: got %s expected (%s, %s)" % [
			label, angles, expected_yaw, expected_pitch])
		quit(1)
