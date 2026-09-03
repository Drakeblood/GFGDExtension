extends GameModeBase
## Rules for the single player test level.
##
## Nothing here spawns the player. _init_game returns false, which lets the
## framework log the local player in and build a pawn from default_pawn_scene -
## set on this game mode's own scene, next to the player state, player controller
## and game state it also decides.
##
## By the time this game mode's _ready runs, the level, its nodes and the player
## have all started. That is where gameplay code goes.


func _init_game(world: World) -> bool:
	var level: Level = world.get_level()
	print("GFGD demo: >> DemoGameMode._init_game | level=%s net_mode=%d" % [level.name, world.get_net_mode()])
	return false    # false = the framework logs in and spawns the default player


func _ready() -> void:
	var player_controller: PlayerController = get_world().get_first_player_controller()
	var pawn: Pawn = player_controller.get_pawn() if player_controller != null else null
	if pawn == null:
		push_warning("GFGD demo: no pawn was possessed")
		return

	var asc: AbilitySystemComponent = pawn.get_pawn_root().get_node("AbilitySystemComponent")
	print("GFGD demo: >> DemoGameMode._ready | player=%s asc_ready=%s" % [pawn.get_pawn_root().name, asc.is_node_ready()])

	asc.attribute_changed.connect(func(attribute_name: StringName, old_value: float, new_value: float) -> void:
		print("GFGD demo:    attribute_changed %s %s -> %s" % [attribute_name, old_value, new_value]))
	asc.owned_tag_changed.connect(func(tag_name: StringName, new_count: int) -> void:
		print("GFGD demo:    owned_tag_changed %s -> %s" % [tag_name, new_count]))

	print("GFGD demo: -- ability demo, health before = %s" % asc.get_attribute_value(&"health"))
	asc.try_activate_ability(&"TestAbility")
	print("GFGD demo: -- ability demo, health after  = %s" % asc.get_attribute_value(&"health"))
