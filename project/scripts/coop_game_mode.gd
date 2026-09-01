extends GameMode
## Demo GameMode for local co-op: two players, two pads, one keyboard ignored.
##
## The framework starts every project with one LocalPlayer holding
## PlayerInput.DEVICE_SLOT_ALL, which is what makes a single player project
## behave exactly as it always did - every device drives the one player. For
## local co-op that wildcard has to go first, or player 0 would swallow the
## events the second pad needs to join with.
##
## Everything below happens in _init_game, before the level enters the tree, so
## both players are logged in and possessed by the time any level node runs
## _ready - the same contract the single player demo relies on.

const MAX_PLAYERS := 2


func _init_game(scene_tree: GFGDSceneTree) -> bool:
	var pads: Array = Input.get_connected_joypads()
	print("GFGD demo: >> CoopGameMode._init_game | connected pads = %s" % [pads])

	_assign_first_player(scene_tree, pads)

	# A second pad that is already plugged in joins straight away. Anything else
	# waits for press-to-join, which the InputRouter offers to try_join().
	if pads.size() > 1 and scene_tree.get_local_player_count() < MAX_PLAYERS:
		scene_tree.create_local_player(pads[1])

	var router: InputRouter = scene_tree.get_input_router()
	router.unassigned_device_input.connect(_on_unassigned_device_input)
	router.joypad_disconnected.connect(_on_joypad_disconnected)

	return false    # false = the framework logs in every local player and spawns them


func _assign_first_player(scene_tree: GFGDSceneTree, pads: Array) -> void:
	var first: LocalPlayer = scene_tree.get_local_player(0)
	if first == null:
		first = scene_tree.create_default_local_player()

	if pads.size() > 0:
		# Two pads and a keyboard, playing on the two pads: player 0 takes the
		# first pad and stops listening to anything else.
		first.device_slots = PackedInt32Array([pads[0]])
	else:
		first.device_slots = PackedInt32Array([PlayerInput.DEVICE_SLOT_KEYBOARD_MOUSE])


func _can_join(device_slot: int) -> bool:
	# This is the answer to "I have two pads and a keyboard and I want to play on
	# the two pads": refuse the keyboard, accept any pad. Without a pad connected
	# the keyboard is all there is, so let it in rather than locking the demo out.
	if device_slot != PlayerInput.DEVICE_SLOT_KEYBOARD_MOUSE:
		return true

	return Input.get_connected_joypads().is_empty()


func _on_post_login(player_controller: PlayerController) -> void:
	var player_state: PlayerState = player_controller.get_player_state()
	var slots: PackedInt32Array = player_controller.get_local_player().device_slots
	print("GFGD demo: >> %s logged in | index=%d devices=%s" % [player_state.player_name, player_state.player_index, slots])


func _on_unassigned_device_input(device_slot: int, _event: InputEvent) -> void:
	print("GFGD demo: -- join offered to device slot %d" % device_slot)


func _on_joypad_disconnected(device: int, local_player: LocalPlayer) -> void:
	if local_player == null:
		return

	print("GFGD demo: -- pad %d unplugged, player %d is waiting for it" % [device, local_player.player_index])


func _ready() -> void:
	var game_state: GameState = get_gfgd_scene_tree().get_game_state()
	print("GFGD demo: >> CoopGameMode._ready | players=%d controllers=%d" % [game_state.get_player_count(), get_player_controllers().size()])
