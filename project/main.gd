extends Control

@onready var file_dialog: FileDialog = $FileDialog
@onready var rom_path_label: Label = $VBoxContainer/HBoxContainer/RomNameLabel
@onready var select_rom_button: Button = $VBoxContainer/HBoxContainer/SelectRomButton

var rom_manager := GodotBridge.new()

# Called when the node enters the scene tree for the first time.
func _ready() -> void:
	select_rom_button.pressed.connect(on_select_button_pressed)
	file_dialog.file_selected.connect(on_rom_file_selected)

	# Configure the file dialog
	file_dialog.access = FileDialog.ACCESS_FILESYSTEM
	file_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	file_dialog.add_filter("*.z64,*.n64,*.v64", "N64 Rom Files")

# Called every frame. 'delta' is the elapsed time since the previous frame.
func _process(delta: float) -> void:
	pass

func on_select_button_pressed() -> void:
	file_dialog.popup_centered(Vector2i(800, 500))

func on_rom_file_selected(path: String) -> void:
	rom_manager.loadROM(path);

	if !rom_manager.ROMLoaded():
		push_error("Can't open this file.")
		return

	rom_path_label.text = path.get_file()
