extends GameMode
## Demo GameMode for the main menu.
##
## A menu has no player, so this overrides _init_game and returns true. That
## suppresses the default spawn completely: there is no PlayerController and no
## pawn at all, rather than an empty one. The GameModeSettings resource this
## runs from leaves pawn_scene unset for the same reason - it is never read.
##
## Wiring the buttons from here rather than from a script on the UI keeps the
## level's rules in the game mode, which is what the game mode is for.

const TEST_LEVEL := "res://levels/test_level.tscn"
const COOP_LEVEL := "res://levels/coop_level.tscn"


func _init_game(scene_tree: GFGDSceneTree) -> bool:
	var level: Level = scene_tree.get_level()
	print("GFGD demo: >> MainMenuGameMode._init_game | level=%s in_tree=%s" % [level.name, level.is_inside_tree()])

	var new_game_button: Button = level.get_node("Menu/Buttons/NewGameButton")
	new_game_button.pressed.connect(_on_new_game_pressed)

	var coop_button: Button = level.get_node("Menu/Buttons/CoopButton")
	coop_button.pressed.connect(_on_coop_pressed)

	var quit_button: Button = level.get_node("Menu/Buttons/QuitButton")
	quit_button.pressed.connect(_on_quit_pressed)

	return true


func _on_new_game_pressed() -> void:
	print("GFGD demo: -- New Game pressed, opening %s" % TEST_LEVEL)
	# open_level frees this level and this game mode with it, and creates the
	# TestLevel game mode before TestLevel enters the tree.
	get_gfgd_scene_tree().open_level(TEST_LEVEL)


func _on_coop_pressed() -> void:
	print("GFGD demo: -- Local Co-op pressed, opening %s" % COOP_LEVEL)
	get_gfgd_scene_tree().open_level(COOP_LEVEL)


func _on_quit_pressed() -> void:
	get_gfgd_scene_tree().quit()
