extends GameModeBase
## Rules for the main menu.
##
## A menu has no player, so this overrides _init_game and returns true. That
## suppresses the default spawn completely: there is no PlayerController and no
## pawn at all, rather than an empty one. This game mode's scene leaves
## default_pawn_scene unset for the same reason - it is never read.
##
## Wiring the buttons from here rather than from a script on the UI keeps the
## level's rules in the game mode, which is what the game mode is for.

const TEST_LEVEL := "res://levels/test_level.tscn"
const COOP_LEVEL := "res://levels/coop_level.tscn"
const ONLINE_LEVEL := "res://levels/online_level.tscn"

var _address_field: LineEdit
var _status: Label


func _init_game(world: World) -> bool:
	var level: Level = world.get_level()
	print("GFGD demo: >> MainMenuGameMode._init_game | level=%s net_mode=%d" % [level.name, world.get_net_mode()])

	var buttons: Node = level.get_node("Menu/Buttons")
	_address_field = buttons.get_node("Address")
	_status = buttons.get_node("Status")

	buttons.get_node("NewGameButton").pressed.connect(_on_new_game_pressed)
	buttons.get_node("CoopButton").pressed.connect(_on_coop_pressed)
	buttons.get_node("HostButton").pressed.connect(_on_host_pressed)
	buttons.get_node("JoinButton").pressed.connect(_on_join_pressed)
	buttons.get_node("QuitButton").pressed.connect(_on_quit_pressed)

	world.connection_failed.connect(func() -> void: _status.text = "Connection failed")
	world.server_disconnected.connect(func() -> void: _status.text = "Server disconnected")

	return true


func _on_new_game_pressed() -> void:
	print("GFGD demo: -- New Game pressed, opening %s" % TEST_LEVEL)
	# open_level frees this level and this game mode with it, and creates the
	# TestLevel game mode before TestLevel starts.
	get_world().open_level(TEST_LEVEL)


func _on_coop_pressed() -> void:
	get_world().open_level(COOP_LEVEL)


func _on_host_pressed() -> void:
	var world: World = get_world()
	var port: int = _port_from_field()
	if world.host_game(port) != OK:
		_status.text = "Could not host on port %d" % port
		return

	# Hosting only opens the door. Travelling is what starts the match, and it is
	# what every client connecting later will be told to load.
	world.server_travel(ONLINE_LEVEL)


func _on_join_pressed() -> void:
	var world: World = get_world()
	_status.text = "Connecting..."
	if world.join_game(_address_from_field(), _port_from_field()) != OK:
		_status.text = "Could not reach that address"


func _address_from_field() -> String:
	var text: String = _address_field.text.strip_edges()
	var colon: int = text.rfind(":")
	return text.substr(0, colon) if colon > 0 else text


func _port_from_field() -> int:
	var text: String = _address_field.text.strip_edges()
	var colon: int = text.rfind(":")
	if colon > 0 and text.substr(colon + 1).is_valid_int():
		return int(text.substr(colon + 1))

	return ProjectSettings.get_setting("application/game_framework/default_port", 7777)


func _on_quit_pressed() -> void:
	get_world().quit()
