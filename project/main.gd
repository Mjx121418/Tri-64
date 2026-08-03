extends Node3D
## 主场景：加载 ROM（通过 GDExtension 的 GodotBridge），后续在此接入
## "ROM → 网格" 的直接提取与渲染（无需先导出 OBJ）。

@onready var open_rom_button: Button = %OpenRomButton
@onready var rom_name_label: Label = %RomNameLabel
@onready var file_dialog: FileDialog = %RomFileDialog
@onready var status_label: Label = %StatusLabel
@onready var object_list: ItemList = %ObjectList
@onready var level_option: OptionButton = %LevelOption
@onready var camera: Camera3D = %Camera3D
@onready var model_root: Node3D = $ModelRoot

@onready var sun: DirectionalLight3D = %Sun
@onready var textures_option: CheckButton = %TexturesOption
@onready var lighting_option: CheckButton = %LightingOption
@onready var shadows_option: CheckButton = %ShadowsOption
@onready var wireframe_option: CheckButton = %WireframeOption

# 关卡编号 = decomp include/level_table.h 的 LevelNum（BOB = 9）。
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
	[30, "Wing Mario Over the Rainbow"],
]

var rom_manager := GodotBridge.new()
var _rom_loaded := false
var selected_level := 9

func _ready() -> void:
	open_rom_button.pressed.connect(_on_open_rom_pressed)
	file_dialog.file_selected.connect(_on_rom_file_selected)
	file_dialog.access = FileDialog.ACCESS_FILESYSTEM
	file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	file_dialog.add_filter("*.z64,*.n64,*.v64", "N64 ROM Files")

	lighting_option.toggled.connect(_on_lighting_toggled)
	shadows_option.toggled.connect(_on_shadows_toggled)
	level_option.item_selected.connect(_on_level_selected)

	for level in LEVELS:
		level_option.add_item("%d  %s" % [level[0], level[1]])
		level_option.set_item_metadata(level_option.item_count - 1, level[0])
	level_option.select(0) # 默认 BOB（9）

	# 渲染选项（Textures / Wireframe）由未来的渲染管线读取，先保留状态。
	status_label.text = "No ROM loaded. Click \"Open ROM\" to select a .z64 file."

func _on_open_rom_pressed() -> void:
	file_dialog.popup_centered(Vector2i(900, 600))

func _on_rom_file_selected(path: String) -> void:
	rom_manager.loadROM(path)
	if not rom_manager.ROMLoaded():
		status_label.text = "Failed to open: %s" % path.get_file()
		return

	_rom_loaded = true
	level_option.disabled = false
	rom_name_label.text = path.get_file()
	_extract_and_render()

func _on_level_selected(index: int) -> void:
	selected_level = level_option.get_item_metadata(index)
	if _rom_loaded:
		_extract_and_render()

func _on_lighting_toggled(enabled: bool) -> void:
	sun.visible = enabled

func _on_shadows_toggled(enabled: bool) -> void:
	sun.shadow_enabled = enabled

## 用 GodotBridge 提取所选关卡并构建 3D 网格（无需导出 OBJ）。
func _extract_and_render() -> void:
	if not rom_manager.extractLevel(selected_level, 1):
		status_label.text = "Extraction failed for level %d." % selected_level
		return

	_clear_model()

	var materials: Array = rom_manager.getMaterials()
	var meshes: Array = rom_manager.getMeshes()
	var objects: Array = rom_manager.getObjects()

	object_list.clear()
	for obj in objects:
		object_list.add_item("obj @ (%.0f, %.0f, %.0f)" % [obj.pos.x, obj.pos.y, obj.pos.z])

	var total_triangles := 0
	for i in meshes.size():
		var md: Dictionary = meshes[i]
		var am := ArrayMesh.new()
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = md.vertices
		arrays[Mesh.ARRAY_NORMAL] = md.normals
		arrays[Mesh.ARRAY_TEX_UV] = md.uvs
		arrays[Mesh.ARRAY_INDEX] = md.indices
		am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

		var mi := MeshInstance3D.new()
		mi.mesh = am
		mi.material_override = _build_material(materials[md.material])
		model_root.add_child(mi)
		total_triangles += md.indices.size() / 3

	# 相机停在原点（0,0,0），手动飞行探索
	camera.global_position = Vector3.ZERO
	camera.rotation_degrees = Vector3(-35, -25, 0)

	status_label.text = "Level %d: %d meshes, %d materials, %d triangles, %d objects." % [
			selected_level, meshes.size(), materials.size(), total_triangles, objects.size()]

func _clear_model() -> void:
	for child in model_root.get_children():
		child.queue_free()

## 根据材质字典构建 StandardMaterial3D。
func _build_material(md: Dictionary) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	# SM64 是无光照渲染（fast3d 的 G_LIGHTING 仅用于物体），导出模型用 Unshaded
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	if md.textured:
		var img := Image.create_from_data(md.tex_width, md.tex_height, false,
				Image.FORMAT_RGBA8, md.tex_pixels)
		mat.albedo_texture = ImageTexture.create_from_image(img)
		if _has_alpha(img):
			mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	else:
		mat.albedo_color = md.color
	return mat

## SM64 的 IA16 圆形/遮罩纹理把形状放在 alpha 里（RGB 可能全黑），
## 有半透明纹素时必须启用 alpha 混合，否则会显示为黑块。
func _has_alpha(img: Image) -> bool:
	for y in img.get_height():
		for x in img.get_width():
			if img.get_pixel(x, y).a < 0.99:
				return true
	return false
