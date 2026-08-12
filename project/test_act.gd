extends SceneTree

## Act 提取测试：默认 act 0 = 所有 act（忽略 OBJECT_WITH_ACTS 掩码），
## 显式 act 1..6 只生成该 act 的对象；act 1 的对象集合是所有 act 的子集。

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
	if not bridge.ROMLoaded():
		push_error("baserom load failed")
		quit(1)
		return
	# 默认（act 0）= 所有 act：原版 BOB 有 10 个 act 专属 OBJECT 放置。
	if not bridge.extractLevel(9, 1):
		push_error("BOB all-acts extraction failed")
		quit(1)
		return
	var all_objects: Array = bridge.getObjects()
	if all_objects.size() != 113:
		push_error("BOB all-acts object count = %d, expected 113" % all_objects.size())
		quit(1)
		return
	# 显式 act 1 = 原版 act-1 黄金值。
	if not bridge.extractLevel(9, 1, 1):
		push_error("BOB act-1 extraction failed")
		quit(1)
		return
	var act1_objects: Array = bridge.getObjects()
	if act1_objects.size() != 103:
		push_error("BOB act-1 object count = %d, expected 103" % act1_objects.size())
		quit(1)
		return
	# act 1 的每个放置（model/behavior/arg/pos/angle）都在所有 act 的集合里。
	var key := func(o: Dictionary) -> String:
		return "%d|%d|%08x|%s|%s" % [o.model, o.behavior_arg, o.behavior, o.pos, o.angle]
	var all_keys := {}
	for o in all_objects:
		all_keys[key.call(o)] = true
	for o in act1_objects:
		if not all_keys.has(key.call(o)):
			push_error("act-1 object missing from all-acts: %s" % key.call(o))
			quit(1)
			return
	# UI 流程：Act 下拉切到 3 → 重新提取。
	main_scene.rom_manager = bridge
	main_scene._rom_loaded = true
	main_scene.act_option.select(3)
	main_scene._on_act_selected(3)
	if main_scene.selected_act != 3:
		push_error("act selection did not apply")
		quit(1)
		return
	var act3_objects: Array = bridge.getObjects()
	if act3_objects.size() >= 113 or act3_objects.size() < 103:
		push_error("BOB act-3 object count = %d, expected in [103, 113)" % act3_objects.size())
		quit(1)
		return
	if not main_scene.status_label.text.contains("Act 3"):
		push_error("status does not show the act: %s" % main_scene.status_label.text)
		quit(1)
		return
	print("act: all=", all_objects.size(), " act1=", act1_objects.size(),
			" act3=", act3_objects.size())
	quit()
