extends GameMode
## Demo GameMode.
##
## Nothing here spawns the player. _init_game returns false, which lets the
## framework spawn and possess the default pawn from GameModeSettings while the
## level is still outside the scene tree - so the player enters the tree
## together with the level and is reachable from every node's _ready.
##
## By the time this game mode's own _ready runs, the level, its nodes and the
## player have all started. That is where gameplay code goes.


func _init_game(scene_tree: GFGDSceneTree) -> bool:
	var level: Level = scene_tree.get_level()
	print("GFGD demo: >> DemoGameMode._init_game | level=%s in_tree=%s" % [level.name, level.is_inside_tree()])
	return false    # false = the framework spawns the default player


func _ready() -> void:
	var player_controller: PlayerController = get_gfgd_scene_tree().get_first_player_controller()
	var pawn_handler: PawnHandler = player_controller.get_pawn_handler() if player_controller != null else null
	if pawn_handler == null:
		push_warning("GFGD demo: no pawn was possessed")
		return

	var asc: AbilitySystemComponent = pawn_handler.get_pawn_root().get_node("AbilitySystemComponent")
	print("GFGD demo: >> DemoGameMode._ready | player=%s asc_ready=%s" % [pawn_handler.get_pawn_root().name, asc.is_node_ready()])

	asc.attribute_changed.connect(func(attribute_name: StringName, old_value: float, new_value: float) -> void:
		print("GFGD demo:    attribute_changed %s %s -> %s" % [attribute_name, old_value, new_value]))
	asc.owned_tag_changed.connect(func(tag_name: StringName, new_count: int) -> void:
		print("GFGD demo:    owned_tag_changed %s -> %s" % [tag_name, new_count]))

	print("GFGD demo: -- ability demo, health before = %s" % asc.get_attribute_value(&"health"))
	asc.try_activate_ability(&"TestAbility")
	print("GFGD demo: -- ability demo, health after  = %s" % asc.get_attribute_value(&"health"))
