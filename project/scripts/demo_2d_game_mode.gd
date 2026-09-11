extends GameModeBase
## Rules for the 2D playgrounds, side-on and top-down: there are none.
##
## _init_game returns false, which lets the framework log the local player in and
## build a pawn from default_pawn_scene - set on this game mode's own scene. The
## level itself does the rest, because everything worth looking at here is the
## movement component's doing rather than a rule's.


func _init_game(world: World) -> bool:
	var level: Level = world.get_level()
	print("GFGD demo: >> 2D game mode _init_game | level=%s net_mode=%d" % [level.name, world.get_net_mode()])
	return false    # false = the framework logs in and spawns the default player
