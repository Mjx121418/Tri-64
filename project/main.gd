extends Node3D
## 主场景：加载 ROM（通过 GDExtension 的 GodotBridge），后续在此接入
## "ROM → 网格" 的直接提取与渲染（无需先导出 OBJ）。

@onready var open_rom_button: Button = %OpenRomButton
@onready var rom_name_label: Label = %RomNameLabel
@onready var file_dialog: FileDialog = %RomFileDialog
@onready var status_label: Label = %StatusLabel
@onready var object_list: ItemList = %ObjectList
@onready var level_option: OptionButton = %LevelOption

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
	# 3D 提取管线接入后：_extract_and_render(path, selected_level)
	status_label.text = "ROM loaded: %s. Extraction pipeline not wired yet." % path.get_file()

func _on_level_selected(index: int) -> void:
	selected_level = level_option.get_item_metadata(index)

func _on_lighting_toggled(enabled: bool) -> void:
	sun.visible = enabled

func _on_shadows_toggled(enabled: bool) -> void:
	sun.shadow_enabled = enabled
