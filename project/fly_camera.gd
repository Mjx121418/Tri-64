extends Camera3D
## 自由飞行相机：点击 3D 视图捕获鼠标，WASD/方向键移动，Space/Ctrl 升降，
## Shift 加速，ESC 释放鼠标回到 UI。

@export var move_speed := 800.0
@export var sprint_multiplier := 4.0
@export var mouse_sensitivity := 0.002

var _captured := false

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT and event.pressed:
		Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)
		_captured = true
	elif event is InputEventKey and event.keycode == KEY_ESCAPE and event.pressed:
		Input.set_mouse_mode(Input.MOUSE_MODE_VISIBLE)
		_captured = false
	elif event is InputEventMouseMotion and _captured:
		# Godot 把鼠标事件变换进控件局部坐标时会把 relative 除以控件的全局
		# 缩放（viewport.cpp 的 _gui_input_event：localizer.basis_xform）。
		# SubViewportContainer 的 scale = 渲染区 / 渲染分辨率：分辨率越高
		# scale 越小 → 相同的屏幕移动得到更大的 relative → 相机更灵敏。
		# 乘回该缩放，使灵敏度与所选分辨率无关（屏幕像素单位）。
		var container := get_viewport().get_parent()
		var scale := 1.0
		if container is SubViewportContainer:
			scale = container.get_global_transform_with_canvas().get_scale().x
		rotate_y(-event.relative.x * mouse_sensitivity * scale)
		rotate_object_local(Vector3.RIGHT, -event.relative.y * mouse_sensitivity * scale)

func _process(delta: float) -> void:
	var speed := move_speed * (sprint_multiplier if Input.is_key_pressed(KEY_SHIFT) else 1.0)

	var forward := -transform.basis.z
	forward.y = 0.0
	forward = forward.normalized()
	var right := transform.basis.x
	right.y = 0.0
	right = right.normalized()

	var dir := Vector3.ZERO
	if Input.is_key_pressed(KEY_W):
		dir += forward
	if Input.is_key_pressed(KEY_S):
		dir -= forward
	if Input.is_key_pressed(KEY_A):
		dir -= right
	if Input.is_key_pressed(KEY_D):
		dir += right
	if Input.is_key_pressed(KEY_R):
		dir += Vector3.UP
	if Input.is_key_pressed(KEY_F):
		dir -= Vector3.UP

	global_position += dir.normalized() * speed * delta
