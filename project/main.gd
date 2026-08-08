extends Node3D
## 主场景：加载 ROM（通过 GDExtension 的 GodotBridge），后续在此接入
## "ROM → 网格" 的直接提取与渲染（无需先导出 OBJ）。

@onready var open_rom_button: Button = %OpenRomButton
@onready var rom_name_label: Label = %RomNameLabel
@onready var file_dialog: FileDialog = %RomFileDialog
@onready var status_label: Label = %StatusLabel
@onready var object_list: ItemList = %ObjectList
@onready var level_option: OptionButton = %LevelOption
@onready var area_option: OptionButton = %AreaOption
@onready var camera: Camera3D = %Camera3D
@onready var model_root: Node3D = $ModelRoot
@onready var camera_pos_label: Label = %CameraPosLabel

@onready var sun: DirectionalLight3D = %Sun
@onready var textures_option: CheckButton = %TexturesOption
@onready var lighting_option: CheckButton = %LightingOption
@onready var shadows_option: CheckButton = %ShadowsOption
@onready var wireframe_option: CheckButton = %WireframeOption
@onready var render_mode_option: OptionButton = %RenderModeOption

# SM64 世界空间光照 shader（受光材质用；Godot 的 NORMAL 含节点旋转 → 对象实例
# 旋转后光照正确）。
const _sm64_lighting_shader := preload("res://sm64_lighting.gdshader")

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

	lighting_option.toggled.connect(_on_lighting_toggled)
	shadows_option.toggled.connect(_on_shadows_toggled)
	level_option.item_selected.connect(_on_level_selected)
	area_option.item_selected.connect(_on_area_selected)
	render_mode_option.item_selected.connect(_on_render_mode_selected)

	render_mode_option.add_item("Geometry")
	render_mode_option.add_item("Collision")
	render_mode_option.select(RENDER_GEOMETRY)

	for level in LEVELS:
		level_option.add_item("%d  %s" % [level[0], level[1]])
		level_option.set_item_metadata(level_option.item_count - 1, level[0])
	level_option.select(0) # 默认 BOB（9）

	# 渲染选项（Textures / Wireframe）由未来的渲染管线读取，先保留状态。
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

func _on_lighting_toggled(enabled: bool) -> void:
	sun.visible = enabled

func _on_shadows_toggled(enabled: bool) -> void:
	sun.shadow_enabled = enabled

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
		return
	_render_current()
	_place_camera()

func _render_current() -> void:
	_clear_model()
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

	object_list.clear()
	for obj in objects:
		var label := "obj 0x%02X @ (%.0f, %.0f, %.0f)" % [
				obj.model, obj.pos.x, obj.pos.y, obj.pos.z]
		object_list.add_item(label)
		object_list.set_item_metadata(object_list.item_count - 1, obj.pos)

	var total_triangles := 0
	for i in meshes.size():
		var md: Dictionary = meshes[i]
		var am := ArrayMesh.new()
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = md.vertices
		arrays[Mesh.ARRAY_NORMAL] = md.normals
		arrays[Mesh.ARRAY_TEX_UV] = md.uvs
		if md.has("colors"):
			arrays[Mesh.ARRAY_COLOR] = md.colors
		arrays[Mesh.ARRAY_INDEX] = md.indices
		am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

		var mi := MeshInstance3D.new()
		mi.mesh = am
		mi.material_override = _build_material(materials[md.material])
		model_root.add_child(mi)
		total_triangles += md.indices.size() / 3

	# 对象模型：同一 model id 的 ArrayMesh/材质只构建一次，所有实例共享。
	# model 0（MODEL_NONE，如传送点）没有几何，跳过。
	var object_models: Array = rom_manager.getObjectModels()
	var model_cache := {}
	for md in object_models:
		model_cache[int(md.model)] = _build_object_mesh(md)

	var rendered_objects := 0
	for obj in objects:
		# 出生状态（行为脚本）：HIDE/DISABLE_RENDERING/DEACTIVATE 的对象不渲染。
		if obj.hidden:
			continue
		var model_id: int = obj.model
		if model_id == 0 or not model_cache.has(model_id):
			continue
		var entry: Dictionary = model_cache[model_id]
		if entry.is_empty():
			continue
		var oi := MeshInstance3D.new()
		oi.mesh = entry.mesh
		oi.position = obj.pos
		oi.rotation = obj.angle
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
		rendered_objects += 1

	status_label.text = "%s, Area %d: %d meshes, %d materials, %d triangles, %d objects (%d rendered)." % [
			rom_manager.getLevelName(), selected_area, meshes.size(), materials.size(), total_triangles,
			objects.size(), rendered_objects]

## 渲染碰撞三角形：受光照的蓝色网格 + 三角形边界（线框）。
func _render_collision() -> void:
	object_list.clear()
	var c: Dictionary = rom_manager.getCollisionTriangles()
	var am := ArrayMesh.new()
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = c.vertices
	arrays[Mesh.ARRAY_NORMAL] = c.normals
	arrays[Mesh.ARRAY_COLOR] = _build_collision_colors(c.classes)
	arrays[Mesh.ARRAY_INDEX] = c.indices
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

	var mi := MeshInstance3D.new()
	mi.mesh = am
	mi.material_override = _build_collision_material()
	model_root.add_child(mi)

	# 三角形边界（加宽线框）：Godot 的 PRIMITIVE_LINES 宽度固定 1px，改为沿每条
	# 边画一条与三角形共面的细带（宽度 COLLISION_EDGE_WIDTH），便于看清薄/透的
	# 碰撞面（如 lavafall）。
	var wire := _build_collision_wireframe(c.vertices, c.indices, c.normals)
	var lam := ArrayMesh.new()
	lam.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, wire)
	var lmi := MeshInstance3D.new()
	lmi.mesh = lam
	lmi.material_override = _build_collision_wireframe_material()
	model_root.add_child(lmi)

	# 对象碰撞三角形（行为 LOAD_COLLISION_DATA，对象本地空间）：与对象模型共用
	# 同一个节点变换（pos/angle），放在对象实际出生位置/朝向。
	var object_triangles := _render_object_collisions()

	status_label.text = "%s, Area %d: collision mode, %d triangles, %d object collision triangles." % [
			rom_manager.getLevelName(), selected_area, c.indices.size() / 3, object_triangles]

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
const COLLISION_EDGE_WIDTH := 10

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

## 碰撞三角形材质：受光照（Per-Pixel）蓝色，双面显示。vertex_color_use_as_albedo
## 开启，颜色由每三角形的 SurfaceClass（floor 蓝 / wall 绿 / ceiling 红）决定。
func _build_collision_material() -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_PER_PIXEL
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
	for child in model_root.get_children():
		child.queue_free()

## 构建对象模型（getObjectModels 单个条目）：多个材质面合并为一个
## ArrayMesh，返回 { mesh, surface_materials }。同一模型的所有对象实例
## 共享这份资源。
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
		if not material_cache.has(mi):
			material_cache[mi] = _build_material(md.materials[mi])
		surface_materials.append(material_cache[mi])
	return {"mesh": am, "surface_materials": surface_materials}

## 根据材质字典构建材质。受光且不透明的材质用世界空间光照 shader（对象实例
## 旋转后光照正确）；其余走 StandardMaterial3D（未纹理 SHADE/PRIM/ENV 或纹理）。
func _build_material(md: Dictionary) -> Material:
	var color_source: int = md.color_source
	var alpha: int = md.alpha
	# 纹理图像（两个路径都用：shader 设 albedo，StandardMaterial3D 设纹理+alpha）。
	var tex_img: Image = null
	if md.textured:
		tex_img = Image.create_from_data(md.tex_width, md.tex_height, false,
				Image.FORMAT_RGBA8, md.tex_pixels)
	# 受光 + 有灯光 + 不透明（纹理无 alpha、材质 alpha 满）→ 光照 shader。
	if md.lit and md.num_lights > 0 and alpha >= 255 \
			and (md.textured and not _has_alpha(tex_img) or not md.textured):
		var light_tex: ImageTexture = null
		if md.textured:
			light_tex = ImageTexture.create_from_image(tex_img)
		return _build_lighting_material(md, light_tex)

	var mat := StandardMaterial3D.new()
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	# SM64 是无光照渲染（fast3d 的 G_LIGHTING 仅用于物体），导出模型用 Unshaded
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	if md.textured:
		mat.albedo_texture = ImageTexture.create_from_image(tex_img)
		# G_SETTILE 的 S/T clamp 模式：任一轴 WRAP 才开启重复，否则关闭
		# （Godot 的重复标志是两轴共用的；SM64 图块两轴模式基本一致）。
		mat.set_flag(BaseMaterial3D.FLAG_USE_TEXTURE_REPEAT,
				md.repeat_s or md.repeat_t)
		if _has_alpha(tex_img):
			mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		# combine 采样 SHADE：texel × 顶点色（shade 由 C++ 端按灯光烘焙）。
		if md.use_vertex:
			mat.vertex_color_use_as_albedo = true
	elif md.use_vertex:
		# 未纹理 + G_CC_SHADE：顶点色（或光照 shade）就是底色（不再是 prim×顶点）。
		mat.albedo_color = Color(1, 1, 1, alpha / 255.0)
		mat.vertex_color_use_as_albedo = true
		if alpha < 255:
			mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	else:
		# 未纹理 + G_CC_PRIMITIVE / G_CC_ENVIRONMENT：底色取 prim/env 颜色。
		if color_source == 5:
			mat.albedo_color = Color(md.env_color.r, md.env_color.g, md.env_color.b,
					alpha / 255.0)
		else:
			mat.albedo_color = Color(md.color.r, md.color.g, md.color.b, alpha / 255.0)
		if alpha < 255:
			mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	return mat

## 受光材质的世界空间光照 shader：shade = ambient + Σ max(0, n̂·l̂)·color，
## 用 Godot 的 NORMAL（含实例旋转）点乘世界空间灯光；texel × shade 或 shade。
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
	return sm

## SM64 的 IA16 圆形/遮罩纹理把形状放在 alpha 里（RGB 可能全黑），
## 有半透明纹素时必须启用 alpha 混合，否则会显示为黑块。
func _has_alpha(img: Image) -> bool:
	for y in img.get_height():
		for x in img.get_width():
			if img.get_pixel(x, y).a < 0.99:
				return true
	return false

## 每帧更新相机位置读数（调试导航用），显示当前位置与朝向的目标点。
func _process(_delta: float) -> void:
	var pos := camera.global_position
	var fwd := -camera.transform.basis.z
	var target := pos + fwd * 1000.0
	camera_pos_label.text = "Cam (%d, %d, %d)  look->(%d, %d, %d)" % [
			roundi(pos.x), roundi(pos.y), roundi(pos.z),
			roundi(target.x), roundi(target.y), roundi(target.z)]

## 点击对象列表：把相机瞬移到该对象的精确位置（便于逐个核对）。
func _on_object_list_item_selected(index: int) -> void:
	var pos: Vector3 = object_list.get_item_metadata(index)
	camera.global_position = pos
