extends Node3D
## 主场景：加载 ROM（通过 GDExtension 的 GodotBridge），后续在此接入
## "ROM → 网格" 的直接提取与渲染（无需先导出 OBJ）。

@onready var open_rom_button: Button = %OpenRomButton
@onready var rom_name_label: Label = %RomNameLabel
@onready var file_dialog: FileDialog = %RomFileDialog
@onready var status_label: Label = %StatusLabel
@onready var object_list: Tree = %ObjectList
@onready var level_option: OptionButton = %LevelOption
@onready var area_option: OptionButton = %AreaOption
@onready var camera: Camera3D = %Camera3D
@onready var model_root: Node3D = $ModelRoot
@onready var camera_pos_label: Label = %CameraPosLabel

@onready var render_mode_option: OptionButton = %RenderModeOption
@onready var room_panel: PanelContainer = %RoomPanel
@onready var room_list: VBoxContainer = %RoomList
@onready var all_rooms_checkbox: CheckButton = %AllRooms
@onready var warning_dialog: AcceptDialog = %WarningDialog
@onready var warning_list: RichTextLabel = %WarningList
@onready var world_env: WorldEnvironment = %WorldEnvironment

# 碰撞房间开关状态（碰撞模式下按房间显示/隐藏静态碰撞表面）。
var _collision_vertices: PackedVector3Array = PackedVector3Array()
var _collision_normals: PackedVector3Array = PackedVector3Array()
var _collision_indices: PackedInt32Array = PackedInt32Array()
var _collision_rooms: PackedInt32Array = PackedInt32Array()
var _collision_colors: PackedColorArray = PackedColorArray()
var _room_checkboxes := {}   # room id -> CheckButton
var _collision_meshes: Array = []  # 静态碰撞 MeshInstance3D（填充 + 线框）
var _visible_collision_triangles := 0

# billboard 节点（对象级 BILLBOARD 标志的 body + 每个 GEO_BILLBOARD 部分）：
# 每帧把 global_basis 设为相机的世界基（游戏把 billboard 渲染成相机空间轴对齐
# ——mtxf_billboard 的 modelview = R_z(roll)，roll≈0 → 始终面向相机）。
var _billboard_nodes: Array = []

# SM64 Fast3D model-view lighting shader（受光材质用）。
const _sm64_lighting_shader := preload("res://sm64_lighting.gdshader")
const _sm64_projected_shader := preload("res://sm64_projected.gdshader")
# SM64 天空盒 shader（skybox.c 的 10×8 图块贴图集，按相机 yaw/pitch 滚动）。
const _skybox_shader := preload("res://skybox.gdshader")

# 天空盒渲染状态：_sky 挂到 WorldEnvironment 的 Sky（background_mode = SKY）；
# 纯色背景改为 BG_COLOR。可见 pass 用屏幕坐标，cubemap pass 用 EYEDIR。
var _sky := Sky.new()
var _sky_material := ShaderMaterial.new()

# 关卡编号 = decomp include/level_table.h 的 LevelNum（BOB = 9）。
# 名称先从 ROM 段2提取；提取失败时回退到这些硬编码名称。
const LEVELS := [
	[4, "Big Boo's Haunt"],
	[5, "Cool, Cool Mountain"],
	[6, "Castle (inside)"],
	[7, "Hazy Maze Cave"],
	[8, "Shifting Sand Land"],
	[9, "Bob-omb Battlefield"],
	[10, "Snowman's Land"],
	[11, "Wet-Dry World"],
	[12, "Jolly Roger Bay"],
	[13, "Tiny-Huge Island"],
	[14, "Tick Tock Clock"],
	[15, "Rainbow Ride"],
	[16, "Castle Grounds"],
	[17, "Bowser in the Dark World"],
	[18, "Vanish Cap Under the Moat"],
	[19, "Bowser in the Fire Sea"],
	[20, "Secret Aquarium"],
	[21, "Bowser in the Sky"],
	[22, "Lethal Lava Land"],
	[23, "Dire, Dire Docks"],
	[24, "Whomp's Fortress"],
	[26, "Castle Courtyard"],
	[27, "Princess's Secret Slide"],
	[28, "Cavern of the Metal Cap"],
	[29, "Tower of the Wing Cap"],
	[30, "Bowser 1"],
	[31, "Wing Mario Over the Rainbow"],
	[33, "Bowser 2"],
	[34, "Bowser 3"],
	[36, "Tall, Tall Mountain"],
]

# 延迟创建 GodotBridge：只在用到时才实例化，且通过类名字符串查找，
# 这样即使 GDExtension 未加载，UI 也能正常工作（按钮至少能打开文件对话框，
# 并给出明确错误，而不是整个脚本初始化失败导致按钮无响应）。
var rom_manager: Variant = null
var _rom_loaded := false
var selected_level := 9
var selected_area := 1

# 渲染模式：0 = 几何三角形（静态网格 + 对象），1 = 碰撞三角形。
const RENDER_GEOMETRY := 0
const RENDER_COLLISION := 1
var render_mode := RENDER_GEOMETRY

func _ensure_bridge() -> bool:
	if rom_manager == null:
		if not ClassDB.class_exists("GodotBridge"):
			return false
		rom_manager = ClassDB.instantiate("GodotBridge")
	return rom_manager != null

func _ready() -> void:
	open_rom_button.pressed.connect(_on_open_rom_pressed)
	file_dialog.file_selected.connect(_on_rom_file_selected)
	file_dialog.access = FileDialog.ACCESS_FILESYSTEM
	file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	file_dialog.add_filter("*.z64,*.n64,*.v64", "N64 ROM Files")

	level_option.item_selected.connect(_on_level_selected)
	area_option.item_selected.connect(_on_area_selected)
	render_mode_option.item_selected.connect(_on_render_mode_selected)
	all_rooms_checkbox.toggled.connect(_on_all_rooms_toggled)

	render_mode_option.add_item("Geometry")
	render_mode_option.add_item("Collision")
	render_mode_option.select(RENDER_GEOMETRY)

	for level in LEVELS:
		level_option.add_item("%d  %s" % [level[0], level[1]])
		level_option.set_item_metadata(level_option.item_count - 1, level[0])
	level_option.select(0) # 默认 BOB（9）

	# 天空盒 shader 挂到 WorldEnvironment 的 Sky（background_mode 在
	# _setup_background 里切换）。
	_sky_material.shader = _skybox_shader
	_sky.sky_material = _sky_material
	world_env.environment.sky = _sky

	status_label.text = "No ROM loaded. Click \"Open ROM\" to select a .z64 file."

func _on_open_rom_pressed() -> void:
	file_dialog.popup_centered_ratio(0.8)

func _on_rom_file_selected(path: String) -> void:
	if not _ensure_bridge():
		status_label.text = "GDExtension (GodotBridge) is not loaded; cannot open the ROM."
		return
	rom_manager.loadROM(path)
	if not rom_manager.ROMLoaded():
		status_label.text = "Failed to open: %s" % path.get_file()
		return

	_rom_loaded = true
	level_option.disabled = false
	rom_name_label.text = path.get_file()
	_populate_levels_from_rom()
	_populate_areas()
	_extract_and_render()

func _on_level_selected(index: int) -> void:
	selected_level = level_option.get_item_metadata(index)
	if _rom_loaded:
		_populate_areas()
		_extract_and_render()

## 从 ROM 一次性加载所有关卡名称并填充下拉列表（替代硬编码名称）。
func _populate_levels_from_rom() -> void:
	if not _ensure_bridge():
		return
	var names: Dictionary = rom_manager.getAllLevelNames()
	var selected := selected_level
	level_option.clear()
	for level in LEVELS:
		var lv: int = level[0]
		var name: String = names.get(lv, "")
		if name.is_empty():
			name = level[1]
		level_option.add_item("%d  %s" % [lv, name])
		level_option.set_item_metadata(level_option.item_count - 1, lv)
	for i in level_option.item_count:
		if level_option.get_item_metadata(i) == selected:
			level_option.select(i)
			break

## 用 getLevelAreas 查询当前关卡的有效区域，填充 Area 下拉列表。
func _populate_areas() -> void:
	area_option.clear()
	area_option.disabled = true
	if not _ensure_bridge():
		return
	var areas: PackedInt32Array = rom_manager.getLevelAreas(selected_level)
	if areas.is_empty():
		return
	area_option.disabled = false
	for area in areas:
		area_option.add_item("Area %d" % area)
		area_option.set_item_metadata(area_option.item_count - 1, area)
	area_option.select(0)
	selected_area = areas[0]

func _on_area_selected(index: int) -> void:
	selected_area = area_option.get_item_metadata(index)
	if _rom_loaded:
		_extract_and_render()

func _on_render_mode_selected(index: int) -> void:
	render_mode = index
	if _rom_loaded:
		_render_current()

## 用 GodotBridge 提取所选关卡，然后按当前渲染模式渲染。
func _extract_and_render() -> void:
	if not _ensure_bridge():
		status_label.text = "GDExtension (GodotBridge) is not loaded; cannot extract the level."
		return
	if not rom_manager.extractLevel(selected_level, selected_area):
		status_label.text = "Extraction failed for level %d." % selected_level
		_show_warnings()
		return
	_apply_projection_context()
	_render_current()
	_place_camera()
	_setup_background()
	_show_warnings()

## Match Godot's perspective frustum to the geo/RSP projection. Position and orientation
## remain under the free-flight camera, but depth and reprojection use the same fov/clip
## range as the extracted area.
func _apply_projection_context() -> void:
	var context: Dictionary = rom_manager.getProjectionContext()
	if context.is_empty() or not context.get("perspective", false):
		return
	camera.fov = float(context.fov)
	camera.near = maxf(0.01, float(context.near))
	camera.far = maxf(camera.near + 1.0, float(context.far))

## 设置当前区域的背景：天空盒（skybox.c 的 10×8 图块贴图集，屏幕 pass 用
## SCREEN_UV、cubemap pass 用 EYEDIR 采样）或纯色填充（geo_process_background
## 的 RGBA5551 填充色）。
func _setup_background() -> void:
	var bg: Dictionary = rom_manager.getBackground()
	var env: Environment = world_env.environment
	if bg.is_skybox and bg.has("skybox_pixels") and bg.skybox_pixels.size() > 0:
		var img := Image.create_from_data(int(bg.skybox_width), int(bg.skybox_height),
				false, Image.FORMAT_RGBA8, bg.skybox_pixels)
		_sky_material.set_shader_parameter("u_sky", ImageTexture.create_from_image(img))
		# sSkyboxColors：只有 JRB 的 ABOVE_CLOUDS 背景在未取得首颗星时
		# 使用深绿遮罩。查看器没有存档状态，因此采用游戏新存档的默认状态。
		if selected_level == 23 and int(bg.background) == 8:
			_sky_material.set_shader_parameter("u_mask",
					Color(0x50 / 255.0, 0x64 / 255.0, 0x5A / 255.0, 1.0))
		else:
			_sky_material.set_shader_parameter("u_mask", Color.WHITE)
		env.background_mode = Environment.BG_SKY
	else:
		env.background_mode = Environment.BG_COLOR
		env.background_color = bg.fill_color

static func _skybox_camera_angles(forward: Vector3) -> Vector2:
	var horizontal := Vector2(forward.x, forward.z).length()
	return Vector2(atan2(forward.x, forward.z), atan2(forward.y, horizontal))

## 提取结束后，把本次提取记录到的警告/被守卫的异常（越界数据、跳过的模型/几何、
## 解码失败等）显示在一个弹窗里；没有警告时不弹。
func _show_warnings() -> void:
	var warnings: Array = rom_manager.getWarnings()
	if warnings.is_empty():
		return
	const MAX_SHOWN := 20
	var lines := PackedStringArray()
	for i in min(warnings.size(), MAX_SHOWN):
		var w: Dictionary = warnings[i]
		lines.append("[b]%s[/b]: %s" % [w.stage, w.message])
	if warnings.size() > MAX_SHOWN:
		lines.append("... and %d more warning(s)." % (warnings.size() - MAX_SHOWN))
	warning_list.text = "\n".join(lines)
	warning_dialog.popup_centered()

func _render_current() -> void:
	_clear_model()
	# 房间开关面板只在碰撞模式显示；无房间数据的关卡在 _populate_room_panel
	# 里进一步隐藏。
	room_panel.visible = render_mode == RENDER_COLLISION
	if render_mode == RENDER_COLLISION:
		_render_collision()
	else:
		_render_geometry()

## 相机只在切换关卡/区域时重新放置；切换渲染模式不移动相机。
func _place_camera() -> void:
	var mario = rom_manager.getMarioStartPos()
	camera.global_position = mario.pos
	camera.global_position = Vector3(0, 0, 0)
	camera.rotation_degrees = Vector3(-35, -25, 0)

## 渲染几何三角形：静态网格 + 对象模型（含特殊对象）。
func _render_geometry() -> void:
	var materials: Array = rom_manager.getMaterials()
	var meshes: Array = rom_manager.getMeshes()
	var objects: Array = rom_manager.getObjects()

	_populate_object_list(objects)

	var total_triangles := 0
	for i in meshes.size():
		var md: Dictionary = meshes[i]
		var am := ArrayMesh.new()
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = md.vertices
		arrays[Mesh.ARRAY_NORMAL] = md.normals
		arrays[Mesh.ARRAY_TEX_UV] = md.uvs
		if md.get("rsp_projected", false):
			arrays[Mesh.ARRAY_TANGENT] = md.rsp_ndc
		if md.has("colors"):
			arrays[Mesh.ARRAY_COLOR] = md.colors
		arrays[Mesh.ARRAY_INDEX] = md.indices
		am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

		var mi := MeshInstance3D.new()
		mi.mesh = am
		mi.material_override = _build_material(materials[md.material], int(md.layer))
		model_root.add_child(mi)
		total_triangles += md.indices.size() / 3

	# 对象模型：同一 model id 的 ArrayMesh/材质只构建一次，所有实例共享。
	# model 0（MODEL_NONE，如传送点）没有几何，跳过。
	var object_models: Array = rom_manager.getObjectModels()
	var model_cache := {}
	for md in object_models:
		model_cache[int(md.model)] = _build_object_mesh(md)
	var inline_objects: Array = rom_manager.getInlineObjectModels()
	var inline_cache := {}
	for md in inline_objects:
		inline_cache[int(md.object)] = md

	var rendered_objects := 0
	for object_index in objects.size():
		var obj: Dictionary = objects[object_index]
		# 出生状态（行为脚本）：HIDE/DISABLE_RENDERING/DEACTIVATE 的对象不渲染。
		if obj.hidden:
			continue
		var model_id: int = obj.model
		if model_id == 0:
			continue
		var entry: Dictionary = model_cache.get(model_id, {})
		if inline_cache.has(object_index):
			_render_inline_object(inline_cache[object_index], obj, entry)
			rendered_objects += 1
			continue
		if entry.is_empty():
			continue
		var oi := MeshInstance3D.new()
		oi.mesh = entry.mesh
		oi.position = obj.pos
		# 对象级 BILLBOARD 标志（行为命令 0x21）：整个对象按 billboard 渲染，
		# face angle 被忽略（geo_process_object 的 mtxf_billboard 分支），每帧
		# 面向相机（见 _billboard_nodes 的更新）。
		oi.rotation = Vector3.ZERO if obj.billboard else obj.angle
		oi.scale = obj.scale
		var surface_materials: Array = entry.surface_materials
		var opacity: int = obj.opacity
		if opacity < 255:
			# 行为显式设置了 oOpacity：克隆材质并应用半透明（不污染共享缓存）。
			for s in surface_materials.size():
				var mat: StandardMaterial3D = surface_materials[s].duplicate()
				mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
				mat.albedo_color.a = opacity / 255.0
				oi.set_surface_override_material(s, mat)
		else:
			for s in surface_materials.size():
				oi.set_surface_override_material(s, surface_materials[s])
		model_root.add_child(oi)
		# GEO_BILLBOARD 部分：游戏把 billboard 的 modelview 设为相机空间的
		# R_z(roll)（mtxf_billboard，roll≈0），位置 = 父链应用到节点平移（pivot）。
		# 每部分一个节点，放在模型空间的 pivot 上（继承实例变换），每帧把朝向
		# 设为相机的世界基（见 _billboard_nodes 更新）。
		if obj.billboard:
			_billboard_nodes.append(oi)
		_add_billboard_parts(oi, entry, opacity)
		rendered_objects += 1

	status_label.text = "%s, Area %d: %d meshes, %d materials, %d triangles, %d objects (%d rendered)." % [
			rom_manager.getLevelName(), selected_area, meshes.size(), materials.size(), total_triangles,
			objects.size(), rendered_objects]

## Inline object meshes already include the object's graph transform. Keep them at the
## world root so Godot does not apply the instance transform a second time. A whole
## object billboard instead gets a camera-facing parent.
func _render_inline_object(inline_md: Dictionary, obj: Dictionary, cached_entry: Dictionary) -> void:
	var opacity: int = obj.opacity
	var object_parent: Node3D = null
	if obj.billboard:
		object_parent = Node3D.new()
		object_parent.position = obj.pos
		object_parent.rotation = Vector3.ZERO
		object_parent.scale = obj.scale
		model_root.add_child(object_parent)
		_billboard_nodes.append(object_parent)
	for me in inline_md.meshes:
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = me.vertices
		arrays[Mesh.ARRAY_NORMAL] = me.normals
		arrays[Mesh.ARRAY_TEX_UV] = me.uvs
		if me.get("rsp_projected", false):
			arrays[Mesh.ARRAY_TANGENT] = me.rsp_ndc
		if me.has("colors"):
			arrays[Mesh.ARRAY_COLOR] = me.colors
		arrays[Mesh.ARRAY_INDEX] = me.indices
		var am := ArrayMesh.new()
		am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		var mi := MeshInstance3D.new()
		mi.mesh = am
		var mat: Material = _build_material(inline_md.materials[int(me.material)], int(me.layer))
		if opacity < 255 and mat is StandardMaterial3D:
			var alpha_mat: StandardMaterial3D = mat.duplicate()
			alpha_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			alpha_mat.albedo_color.a = opacity / 255.0
			mat = alpha_mat
		mi.material_override = mat
		if object_parent != null:
			object_parent.add_child(mi)
		else:
			model_root.add_child(mi)

	if inline_md.has("billboard_parts") and not inline_md.billboard_parts.is_empty():
		var parent := object_parent
		if parent == null:
			parent = Node3D.new()
			parent.position = obj.pos
			parent.rotation = obj.angle
			parent.scale = obj.scale
			model_root.add_child(parent)
		_add_inline_billboard_parts(parent, inline_md.billboard_parts, opacity)
	elif cached_entry.has("billboard_parts") and not cached_entry.billboard_parts.is_empty():
		var parent := Node3D.new()
		parent.position = obj.pos
		parent.rotation = obj.angle
		parent.scale = obj.scale
		model_root.add_child(parent)
		_add_billboard_parts(parent, cached_entry, opacity)

func _add_inline_billboard_parts(parent: Node3D, parts: Array, opacity: int) -> void:
	for part in parts:
		for me in part.meshes:
			var arrays := []
			arrays.resize(Mesh.ARRAY_MAX)
			arrays[Mesh.ARRAY_VERTEX] = me.vertices
			arrays[Mesh.ARRAY_NORMAL] = me.normals
			arrays[Mesh.ARRAY_TEX_UV] = me.uvs
			if me.has("colors"):
				arrays[Mesh.ARRAY_COLOR] = me.colors
			arrays[Mesh.ARRAY_INDEX] = me.indices
			var am := ArrayMesh.new()
			am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
			var bi := MeshInstance3D.new()
			bi.mesh = am
			bi.position = part.pivot
			var mat: Material = _build_material(part.materials[int(me.material)], int(me.layer))
			if opacity < 255 and mat is StandardMaterial3D:
				var alpha_mat: StandardMaterial3D = mat.duplicate()
				alpha_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
				alpha_mat.albedo_color.a = opacity / 255.0
				mat = alpha_mat
			bi.material_override = mat
			parent.add_child(bi)
			_billboard_nodes.append(bi)

func _add_billboard_parts(parent: Node3D, entry: Dictionary, opacity: int) -> void:
	if not entry.has("billboard_parts") or entry.billboard_parts.is_empty():
		return
	for part in entry.billboard_parts:
		var bi := MeshInstance3D.new()
		bi.mesh = part.mesh
		bi.position = part.pivot
		var part_materials: Array = part.surface_materials
		if opacity < 255:
			for s in part_materials.size():
				var mat: StandardMaterial3D = part_materials[s].duplicate()
				mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
				mat.albedo_color.a = opacity / 255.0
				bi.set_surface_override_material(s, mat)
		else:
			for s in part_materials.size():
				bi.set_surface_override_material(s, part_materials[s])
		parent.add_child(bi)
		_billboard_nodes.append(bi)

## 按 model id 分组对象；每组是一个可展开的树节点，组按 id 数值升序排列。
func _populate_object_list(objects: Array) -> void:
	object_list.clear()
	var groups := {}
	for object_index in objects.size():
		var obj: Dictionary = objects[object_index]
		var model_id := int(obj.model)
		if not groups.has(model_id):
			groups[model_id] = []
		groups[model_id].append({"index": object_index, "object": obj})

	var root := object_list.create_item()
	var model_ids: Array = groups.keys()
	model_ids.sort()
	for model_id in model_ids:
		var group_item := object_list.create_item(root)
		var group_objects: Array = groups[model_id]
		group_item.set_text(0, "Model 0x%02X (%d objects)" % [model_id, group_objects.size()])
		group_item.set_selectable(0, false)
		group_item.collapsed = true
		for entry in group_objects:
			var obj: Dictionary = entry.object
			var child := object_list.create_item(group_item)
			child.set_text(0, "Object %d @ (%.0f, %.0f, %.0f)" % [
					entry.index, obj.pos.x, obj.pos.y, obj.pos.z])
			child.set_metadata(0, obj.pos)

## 渲染碰撞三角形：受光照的网格 + 三角形边界（线框）。表面按房间开关过滤
##（RoomPanel 的每房间 CheckButton；默认全开）。
func _render_collision() -> void:
	object_list.clear()
	_collision_meshes.clear()
	var c: Dictionary = rom_manager.getCollisionTriangles()
	_collision_vertices = c.vertices
	_collision_normals = c.normals
	_collision_rooms = c.rooms
	_collision_indices = c.indices
	_collision_colors = _build_collision_colors(c.classes)
	_populate_room_panel()
	_rebuild_collision_mesh()

	# 对象碰撞三角形（行为 LOAD_COLLISION_DATA，对象本地空间）：与对象模型共用
	# 同一个节点变换（pos/angle），放在对象实际出生位置/朝向。
	var object_triangles := _render_object_collisions()

	status_label.text = "%s, Area %d: collision mode, %d/%d triangles shown, %d object collision triangles." % [
			rom_manager.getLevelName(), selected_area, _visible_collision_triangles,
			_collision_indices.size() / 3, object_triangles]

## 从 _collision_rooms 收集唯一房间号，为每个房间加一个 CheckButton。无房间
## 数据（全部 room 0）时隐藏面板（没有可切换的房间）。
func _populate_room_panel() -> void:
	for child in room_list.get_children():
		child.queue_free()
	_room_checkboxes.clear()
	var uniq := {}
	for r in _collision_rooms:
		uniq[int(r)] = true
	var ids: Array = uniq.keys()
	ids.sort()
	if ids.size() == 1 and int(ids[0]) == 0:
		room_panel.visible = false
		return
	room_panel.visible = true
	all_rooms_checkbox.set_pressed_no_signal(true)
	for id in ids:
		var cb := CheckButton.new()
		cb.text = "Room %d" % id
		cb.button_pressed = true
		cb.focus_mode = Control.FOCUS_NONE
		cb.toggled.connect(_on_room_toggled)
		_room_checkboxes[int(id)] = cb
		room_list.add_child(cb)

func _on_all_rooms_toggled(pressed: bool) -> void:
	for cb in _room_checkboxes.values():
		cb.set_pressed_no_signal(pressed)
	_rebuild_collision_mesh()

func _on_room_toggled(_pressed: bool) -> void:
	_rebuild_collision_mesh()

## 用当前房间开关过滤 _collision_* 静态碰撞数组，重建填充网格 + 线框。
func _rebuild_collision_mesh() -> void:
	for m in _collision_meshes:
		if is_instance_valid(m):
			m.free()
	_collision_meshes.clear()
	var fv := PackedVector3Array()
	var fn := PackedVector3Array()
	var fc := PackedColorArray()
	var fi := PackedInt32Array()
	var ntris := _collision_indices.size() / 3
	for t in range(ntris):
		var room := int(_collision_rooms[t])
		var cb: CheckButton = _room_checkboxes.get(room)
		if cb != null and not cb.button_pressed:
			continue
		var i0 := _collision_indices[t * 3]
		var i1 := _collision_indices[t * 3 + 1]
		var i2 := _collision_indices[t * 3 + 2]
		var base := fv.size()
		fv.push_back(_collision_vertices[i0])
		fv.push_back(_collision_vertices[i1])
		fv.push_back(_collision_vertices[i2])
		fn.push_back(_collision_normals[i0])
		fn.push_back(_collision_normals[i1])
		fn.push_back(_collision_normals[i2])
		fc.push_back(_collision_colors[i0])
		fc.push_back(_collision_colors[i1])
		fc.push_back(_collision_colors[i2])
		fi.push_back(base)
		fi.push_back(base + 1)
		fi.push_back(base + 2)
	_visible_collision_triangles = fi.size() / 3
	if fv.is_empty():
		return
	var am := ArrayMesh.new()
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = fv
	arrays[Mesh.ARRAY_NORMAL] = fn
	arrays[Mesh.ARRAY_COLOR] = fc
	arrays[Mesh.ARRAY_INDEX] = fi
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var mi := MeshInstance3D.new()
	mi.mesh = am
	mi.material_override = _build_collision_material()
	model_root.add_child(mi)
	_collision_meshes.append(mi)

	# 三角形边界（加宽线框）：Godot 的 PRIMITIVE_LINES 宽度固定 1px，改为沿每条
	# 边画一条与三角形共面的细带（宽度 COLLISION_EDGE_WIDTH），便于看清薄/透的
	# 碰撞面（如 lavafall）。
	var wire := _build_collision_wireframe(fv, fi, fn)
	var lam := ArrayMesh.new()
	lam.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, wire)
	var lmi := MeshInstance3D.new()
	lmi.mesh = lam
	lmi.material_override = _build_collision_wireframe_material()
	model_root.add_child(lmi)
	_collision_meshes.append(lmi)

## 渲染对象的碰撞三角形（每个有 LOAD_COLLISION_DATA 的对象一个网格），放在
## 对象的出生位置/朝向。返回对象碰撞三角形总数。
func _render_object_collisions() -> int:
	var fill_mat := _build_collision_material()
	var wire_mat := _build_collision_wireframe_material()
	var total := 0
	for oc in rom_manager.getObjectCollisions():
		var am := ArrayMesh.new()
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = oc.vertices
		arrays[Mesh.ARRAY_NORMAL] = oc.normals
		arrays[Mesh.ARRAY_COLOR] = _build_collision_colors(oc.classes)
		arrays[Mesh.ARRAY_INDEX] = oc.indices
		am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

		var mi := MeshInstance3D.new()
		mi.mesh = am
		mi.position = oc.pos
		mi.rotation = oc.angle
		mi.material_override = fill_mat
		model_root.add_child(mi)
		total += oc.indices.size() / 3

		var wire := _build_collision_wireframe(oc.vertices, oc.indices, oc.normals)
		var lam := ArrayMesh.new()
		lam.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, wire)
		var lmi := MeshInstance3D.new()
		lmi.mesh = lam
		lmi.position = oc.pos
		lmi.rotation = oc.angle
		lmi.material_override = wire_mat
		model_root.add_child(lmi)
	return total

## 把碰撞三角形每条边扩展成一条细带（共面，法线向外偏移避免与填充面 z-fight）。
## 返回 ArrayMesh 的 arrays（PRIMITIVE_TRIANGLES）。
const COLLISION_EDGE_WIDTH := 5

func _build_collision_wireframe(verts: PackedVector3Array, indices: PackedInt32Array,
		normals: PackedVector3Array) -> Array:
	var qv := PackedVector3Array()
	var qn := PackedVector3Array()
	var qi := PackedInt32Array()
	for t in range(0, indices.size(), 3):
		var i0: int = indices[t]
		var i1: int = indices[t + 1]
		var i2: int = indices[t + 2]
		var a := verts[i0]
		var b := verts[i1]
		var c := verts[i2]
		var n := normals[i0]
		_add_edge_ribbon(qv, qn, qi, a, b, n)
		_add_edge_ribbon(qv, qn, qi, b, c, n)
		_add_edge_ribbon(qv, qn, qi, c, a, n)
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = qv
	arrays[Mesh.ARRAY_NORMAL] = qn
	arrays[Mesh.ARRAY_INDEX] = qi
	return arrays

## 沿边 (p1,p2) 生成一条细带（两个三角形）：三角形平面内垂直于边的方向 e 展开
## 半宽，整体沿法线向外偏移少许（避免与填充面 z-fight）。
func _add_edge_ribbon(qv: PackedVector3Array, qn: PackedVector3Array,
		qi: PackedInt32Array, p1: Vector3, p2: Vector3, n: Vector3) -> void:
	var d := (p2 - p1).normalized()
	var e: Vector3
	if n.cross(d).length() < 1e-6:
		e = Vector3.RIGHT * (COLLISION_EDGE_WIDTH * 0.5)
	else:
		e = n.cross(d).normalized() * (COLLISION_EDGE_WIDTH * 0.5)
	var off := n * (COLLISION_EDGE_WIDTH * 0.25)
	var base: int = qv.size()
	qv.append(p1 + off + e)
	qv.append(p2 + off + e)
	qv.append(p2 + off - e)
	qv.append(p1 + off - e)
	qn.append(n); qn.append(n); qn.append(n); qn.append(n)
	qi.append(base); qi.append(base + 1); qi.append(base + 2)
	qi.append(base); qi.append(base + 2); qi.append(base + 3)

## 碰撞三角形材质：不受场景光照影响，双面显示。vertex_color_use_as_albedo
## 开启，颜色由每三角形的 SurfaceClass（floor 蓝 / wall 绿 / ceiling 红）决定。
func _build_collision_material() -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = Color.WHITE
	mat.vertex_color_use_as_albedo = true
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	return mat

## 每三角形 SurfaceClass → 顶点颜色（平坦着色：3 顶点共享同一颜色）。
## classes 为每三角形 1 个值（indices.size()/3）。
func _build_collision_colors(classes: PackedInt32Array) -> PackedColorArray:
	var pal := [
		Color(0.25, 0.45, 1.0), # SURFACE_CLASS_FLOOR 蓝
		Color(0.9, 0.2, 0.2),   # SURFACE_CLASS_CEILING 红
		Color(0.2, 0.8, 0.3),   # SURFACE_CLASS_WALL 绿
	]
	var colors := PackedColorArray()
	colors.resize(classes.size() * 3)
	for t in classes.size():
		var col: Color = pal[classes[t]]
		var i := t * 3
		colors[i] = col
		colors[i + 1] = col
		colors[i + 2] = col
	return colors

## 碰撞三角形边界材质：深色线条，双面显示。
func _build_collision_wireframe_material() -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = Color(0.0, 0.0, 0.0)
	return mat

func _clear_model() -> void:
	_billboard_nodes.clear()
	for child in model_root.get_children():
		child.queue_free()

## 构建对象模型（getObjectModels 单个条目）：多个材质面合并为一个
## ArrayMesh，返回 { mesh, surface_materials, billboard_parts }。同一模型的
## 所有对象实例共享这份资源。billboard_parts 是 GEO_BILLBOARD 子树的
## { pivot, mesh, surface_materials } 列表（每部分一个节点，面向相机）。
func _build_object_mesh(md: Dictionary) -> Dictionary:
	var am := ArrayMesh.new()
	var surface_materials: Array[Material] = []
	var material_cache := {}
	for me in md.meshes:
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = me.vertices
		arrays[Mesh.ARRAY_NORMAL] = me.normals
		arrays[Mesh.ARRAY_TEX_UV] = me.uvs
		if me.has("colors"):
			arrays[Mesh.ARRAY_COLOR] = me.colors
		arrays[Mesh.ARRAY_INDEX] = me.indices
		am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		var mi: int = me.material
		# 材质缓存键含绘制层（同材质不同层的表面材质不同：透明度/深度写）。
		var mkey := [mi, int(me.layer)]
		if not material_cache.has(mkey):
			material_cache[mkey] = _build_material(md.materials[mi], int(me.layer))
		surface_materials.append(material_cache[mkey])
	var entry := {"mesh": am, "surface_materials": surface_materials}
	# billboard 部分（每 GEO_BILLBOARD 节点一个）：{ pivot, mesh,
	# surface_materials }。材质表与 body 独立（C++ 端各自去重），索引互不相通，
	# 每个 part 用单独的缓存（纹理图像可能重复，重复解码无害）。
	if md.has("billboard_parts") and not md.billboard_parts.is_empty():
		var parts: Array = []
		for part in md.billboard_parts:
			var bam := ArrayMesh.new()
			var part_materials: Array[Material] = []
			var part_cache := {}
			for me in part.meshes:
				var arrays := []
				arrays.resize(Mesh.ARRAY_MAX)
				arrays[Mesh.ARRAY_VERTEX] = me.vertices
				arrays[Mesh.ARRAY_NORMAL] = me.normals
				arrays[Mesh.ARRAY_TEX_UV] = me.uvs
				if me.has("colors"):
					arrays[Mesh.ARRAY_COLOR] = me.colors
				arrays[Mesh.ARRAY_INDEX] = me.indices
				bam.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
				var mi: int = me.material
				var mkey := [mi, int(me.layer)]
				if not part_cache.has(mkey):
					part_cache[mkey] = _build_material(part.materials[mi], int(me.layer))
				part_materials.append(part_cache[mkey])
				parts.append({"pivot": part.pivot, "mesh": bam,
						"surface_materials": part_materials})
		entry.billboard_parts = parts
	return entry

## 根据材质字典 + 绘制层构建材质。层语义（游戏 renderModeTable_1Cycle/2Cycle，
## 见 Engine.md §5）：0-3 OPA（忽略 alpha，强制不透明，深度写开）、4 TEX_EDGE
##（alpha scissor——硬边裁剪，深度写开）、5-7 XLU（alpha 混合，深度写关）。
## 受光且不透明的材质用 Fast3D model-view 光照 shader；静态区域和对象都保留
## 相机参与的 model-view 光照，不能只使用提取时烘焙的 SHADE。
## 其余走 StandardMaterial3D。
func _build_material(md: Dictionary, layer: int = 0) -> Material:
	var color_source: int = md.color_source
	var alpha: int = md.alpha
	# 纹理图像（两个路径都用：shader 设 albedo，StandardMaterial3D 设纹理+alpha）。
	var tex_img: Image = null
	var tex_alpha := false
	if md.textured:
		tex_img = Image.create_from_data(md.tex_width, md.tex_height, false,
				Image.FORMAT_RGBA8, md.tex_pixels)
		tex_alpha = _has_alpha(tex_img)
	# OPA 层（0-3）的混合被关闭：纹理/顶点/prim alpha 都不参与（G_RM_*_OPA_SURF）。
	var alpha_matters := layer >= 4
	# 透明模式：层 4 = alpha scissor（G_RM_AA_TEX_EDGE 的边缘 alpha → 硬边裁剪，
	# 走不透明渲染管线，深度照常写）；层 5-7 = alpha 混合（G_RM_*_XLU_SURF）。
	var alpha_blend := layer >= 5
	# 受光 + 有灯光 + 不透明（OPA 层忽略纹理 alpha；其余层纹理无 alpha、材质
	# alpha 满）→ 光照 shader。
	if md.get("rsp_projection", false) and layer < 4:
		return _build_projected_material(md, tex_img)
	if md.lit and md.num_lights > 0 and alpha >= 255 \
			and (md.textured and (not alpha_matters or not tex_alpha) or not md.textured):
		var light_tex: ImageTexture = null
		if md.textured:
			light_tex = ImageTexture.create_from_image(tex_img)
		return _build_lighting_material(md, light_tex)

	var mat := StandardMaterial3D.new()
	mat.cull_mode = (BaseMaterial3D.CULL_FRONT if md.get("cull_back", true)
			else BaseMaterial3D.CULL_DISABLED)
	# SM64 是无光照渲染（fast3d 的 G_LIGHTING 仅用于物体），导出模型用 Unshaded
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	if md.textured:
		mat.albedo_texture = ImageTexture.create_from_image(tex_img)
		# G_SETTILE 的 S/T clamp 模式：任一轴 WRAP 才开启重复，否则关闭
		# （Godot 的重复标志是两轴共用的；SM64 图块两轴模式基本一致）。
		mat.set_flag(BaseMaterial3D.FLAG_USE_TEXTURE_REPEAT,
				md.repeat_s or md.repeat_t)
		if alpha_matters and tex_alpha:
			mat.transparency = (BaseMaterial3D.TRANSPARENCY_ALPHA if alpha_blend
					else BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR)
		# combine 采样 SHADE：texel × 顶点色（shade 由 C++ 端按灯光烘焙）。
		if md.use_vertex:
			mat.vertex_color_use_as_albedo = true
	elif md.use_vertex:
		# 未纹理 + G_CC_SHADE：顶点色（或光照 shade）就是底色（不再是 prim×顶点）。
		mat.albedo_color = Color(1, 1, 1, alpha / 255.0 if alpha_matters else 1.0)
		mat.vertex_color_use_as_albedo = true
		if alpha_matters and alpha < 255:
			mat.transparency = (BaseMaterial3D.TRANSPARENCY_ALPHA if alpha_blend
					else BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR)
	else:
		# 未纹理 + G_CC_PRIMITIVE / G_CC_ENVIRONMENT：底色取 prim/env 颜色。
		if color_source == 5:
			mat.albedo_color = Color(md.env_color.r, md.env_color.g, md.env_color.b,
					alpha / 255.0 if alpha_matters else 1.0)
		else:
			mat.albedo_color = Color(md.color.r, md.color.g, md.color.b,
					alpha / 255.0 if alpha_matters else 1.0)
		if alpha_matters and alpha < 255:
			mat.transparency = (BaseMaterial3D.TRANSPARENCY_ALPHA if alpha_blend
					else BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR)
	# XLU 层（5-7）：G_RM_*_XLU_SURF 只读深度不写（Z_CMP 无 Z_UPD）。
	if layer >= 5:
		mat.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
	return mat

## Static area geometry shader. The captured Fast3D NDC/depth remains in the
## TANGENT channel for diagnostics; the active branch reprojects with Godot's
## current camera so movement remains live.
func _build_projected_material(md: Dictionary, tex_img: Image) -> ShaderMaterial:
	var sm := ShaderMaterial.new()
	sm.shader = _sm64_projected_shader
	if tex_img != null:
		sm.set_shader_parameter("albedo_tex", ImageTexture.create_from_image(tex_img))
		sm.set_shader_parameter("use_texel", true)
		sm.set_shader_parameter("clamp_uv", not (md.repeat_s or md.repeat_t))
		if md.has("uv_clamp"):
			sm.set_shader_parameter("uv_clamp", md.uv_clamp)
	else:
		sm.set_shader_parameter("use_texel", false)
	var base_color: Color = md.env_color if int(md.color_source) == 5 else md.color
	sm.set_shader_parameter("base_color", Vector3(base_color.r, base_color.g, base_color.b))
	sm.set_shader_parameter("use_vertex", md.use_vertex)
	var dynamic_lighting: bool = md.lit and md.use_vertex \
			and int(md.get("num_lights", 0)) > 0
	sm.set_shader_parameter("use_dynamic_lighting", dynamic_lighting)
	sm.set_shader_parameter("num_lights", int(md.get("num_lights", 0)))
	sm.set_shader_parameter("cull_back", md.get("cull_back", true))
	if md.has("ambient"):
		sm.set_shader_parameter("ambient", md.ambient)
	if md.has("light_dirs"):
		sm.set_shader_parameter("light_dirs", md.light_dirs)
	if md.has("light_cols"):
		sm.set_shader_parameter("light_cols", md.light_cols)
	# Fixed NDC is extraction-camera specific; keep the dynamic camera branches active.
	sm.set_shader_parameter("use_rsp_position", false)
	sm.set_shader_parameter("use_dynamic_reprojection", true)
	return sm

## 受光材质的 Fast3D model-view 光照 shader：shade = ambient + Σ max(0, n̂·l̂)·color；
## 保持原始 Light_t 方向并按当前 MODELVIEW_MATRIX 变换；texel × shade 或 shade。
func _build_lighting_material(md: Dictionary, tex: ImageTexture) -> ShaderMaterial:
	var sm := ShaderMaterial.new()
	sm.shader = _sm64_lighting_shader
	if tex != null:
		sm.set_shader_parameter("albedo_tex", tex)
		sm.set_shader_parameter("use_texel", true)
		# 两轴都 CLAMP 才关重复（shader 里用 UV clamp；与 StandardMaterial3D 的
		# FLAG_USE_TEXTURE_REPEAT 近似一致）。
		sm.set_shader_parameter("clamp_uv", not (md.repeat_s or md.repeat_t))
		if md.has("uv_clamp"):
			sm.set_shader_parameter("uv_clamp", md.uv_clamp)
	else:
		sm.set_shader_parameter("use_texel", false)
	sm.set_shader_parameter("num_lights", md.num_lights)
	sm.set_shader_parameter("ambient", md.ambient)
	sm.set_shader_parameter("light_dirs", md.light_dirs)
	sm.set_shader_parameter("light_cols", md.light_cols)
	sm.set_shader_parameter("cull_back", md.get("cull_back", true))
	# The game recomputes light directions after the camera model-view is active.
	# The captured SHADE remains available in vertex colors for export/tests, but is
	# not the renderer's final light input for this live path.
	sm.set_shader_parameter("use_vertex_shade", false)
	return sm

## SM64 的 IA16 圆形/遮罩纹理把形状放在 alpha 里（RGB 可能全黑），
## 有半透明纹素时必须启用 alpha 混合，否则会显示为黑块。
func _has_alpha(img: Image) -> bool:
	for y in img.get_height():
		for x in img.get_width():
			if img.get_pixel(x, y).a < 0.99:
				return true
	return false

## 每帧更新相机位置读数（调试导航用），显示当前位置与朝向的目标点；
## billboard 节点朝向相机：游戏把 billboard 的 modelview 设为相机空间的
## R_z(roll)（geo_process_billboard 的 mtxf_billboard，roll≈0 = 恒等），所以
## 世界朝向 = 视图矩阵的逆 = 相机的世界基（camera.global_transform.basis）。
## 节点的局部位置 = pivot，继承实例变换 → 位置跟随父链。
func _process(_delta: float) -> void:
	var pos := camera.global_position
	var fwd := -camera.transform.basis.z
	var target := pos + fwd * 1000.0
	camera_pos_label.text = "Cam (%d, %d, %d)  look->(%d, %d, %d)" % [
			roundi(pos.x), roundi(pos.y), roundi(pos.z),
			roundi(target.x), roundi(target.y), roundi(target.z)]
	if not _billboard_nodes.is_empty():
		var cam_basis := camera.global_transform.basis
		for node in _billboard_nodes:
			node.global_basis = cam_basis
	# 天空盒：逐帧传 decomp skybox.c 的 yaw/pitch。SM64 的 atan2s(y, x)
	# 角度约定等价于标准 atan2(x, y)，所以 yaw = atan2(x, z)，pitch =
	# atan2(y, horizontal)。天空是世界固定的：右转 → 天空内容左移。
	var sky_angles := _skybox_camera_angles(fwd)
	_sky_material.set_shader_parameter("u_cam_yaw", sky_angles.x)
	_sky_material.set_shader_parameter("u_cam_pitch", sky_angles.y)

## 点击对象列表中的对象：把相机瞬移到该对象的精确位置（便于逐个核对）。
func _on_object_list_cell_selected() -> void:
	var item := object_list.get_selected()
	if item == null:
		return
	var pos: Variant = item.get_metadata(0)
	if pos is Vector3:
		camera.global_position = pos
