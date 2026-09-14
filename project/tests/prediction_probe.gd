extends Node
# The hands and eyes of test_prediction.gd, loaded into both of its processes
# through the demo's --probe flag. It sits at /root/Probe on the server and on
# the client, which is what lets the two talk: a remote call only arrives if
# the node sending it and the node receiving it are at the same path.
#
# The server half does one thing on request: it moves the client's character
# sideways, as a respawn or a hit would - a change the client cannot have
# predicted. The client half drives its own character through a scripted
# sequence and checks what prediction promises:
#
#   - the character moves on the tick a key goes down, not a round trip later
#   - a jump and a dash - the dash sent as a custom input flag - are predicted
#     on the tick they are pressed
#   - with the same inputs the server agrees exactly, so a stretch of walking,
#     jumping, dashing and running into a wall produces no correction at all,
#     whatever the network does to the packets
#   - a change the server makes is corrected once, by about the size it was,
#     with about a round trip's worth of moves replayed, and is followed by
#     agreement again rather than by a chain of corrections
#
# Every check prints PASS or FAIL and the client quits with the result as its
# exit code, which is what the runner reports.

const KICK := Vector3(2.0, 0.0, 0.0)

# Seconds before either side gives up and quits on its own, so a stuck run can
# never hang the runner.
const CLIENT_DEADLINE := 40.0
const SERVER_DEADLINE := 60.0

enum Step { WAIT, SETTLE, RESPOND, SCRIPT, DRAIN, KICK_WAIT, AFTER, DONE }

var world: World
var failures: Array[String] = []
var elapsed := 0.0

var step := Step.WAIT
var step_ticks := 0

var movement: CharacterMovementComponent
var backend: PredictedMovementBackend
var player_input: PlayerInput
var root_3d: Node3D

var corrections: Array[Dictionary] = []
var max_ack_error := 0.0
var seen_acks := 0
var baseline_corrections := 0
var mark := Vector3.ZERO
var kick_correction: Dictionary = {}


func _ready() -> void:
	world = get_tree() as World
	process_priority = -100
	process_physics_priority = -100

	rpc_config(&"server_kick", {
		"rpc_mode": MultiplayerAPI.RPC_MODE_ANY_PEER,
		"transfer_mode": MultiplayerPeer.TRANSFER_MODE_RELIABLE,
		"call_local": false,
		"channel": 0,
	})


func _check(name: String, complaint: String) -> void:
	print("    %s %s%s" % ["PASS" if complaint.is_empty() else "FAIL", name,
		"" if complaint.is_empty() else "  -  " + complaint])
	if not complaint.is_empty():
		failures.append("%s: %s" % [name, complaint])


func _process(delta: float) -> void:
	elapsed += delta
	if world == null:
		return

	if world.has_authority():
		if elapsed > SERVER_DEADLINE:
			print("prediction probe (server): deadline reached, quitting")
			world.quit(0)
		return

	if elapsed > CLIENT_DEADLINE and step != Step.DONE:
		_check("the run finished in time", "stuck at step %s" % Step.keys()[step])
		_finish()


# --- Server --------------------------------------------------------------------

func server_kick() -> void:
	var sender: int = multiplayer.get_remote_sender_id()
	for pawn_root in world.get_pawn_container().get_children():
		var pawn: Pawn = pawn_root.get_node_or_null(^"Pawn")
		if pawn != null and pawn.get_owner_peer_id() == sender:
			(pawn_root as Node3D).global_position += KICK
			print("prediction probe (server): moved %s by %s" % [pawn_root.name, KICK])


# --- Client --------------------------------------------------------------------

func _physics_process(_delta: float) -> void:
	if world == null or world.has_authority():
		return

	step_ticks += 1

	# Only when an acknowledgement has actually been processed since the last
	# look - otherwise a tick with none would read the previous one again.
	if backend != null and backend.get_ack_count() != seen_acks:
		seen_acks = backend.get_ack_count()
		max_ack_error = maxf(max_ack_error, backend.get_last_ack_error())

	match step:
		Step.WAIT:
			_wait_for_pawn()
		Step.SETTLE:
			_settle()
		Step.RESPOND:
			_respond()
		Step.SCRIPT:
			_script()
		Step.DRAIN:
			_drain()
		Step.KICK_WAIT:
			_kick_wait()
		Step.AFTER:
			_after()


func _next(next_step: Step) -> void:
	step = next_step
	step_ticks = 0


func _press(action: StringName) -> void:
	player_input.action_press(action, 1.0)


func _release(action: StringName) -> void:
	player_input.action_release(action)


func _release_all() -> void:
	for action in [&"move_left", &"move_right", &"move_forward", &"move_back", &"jump", &"dash"]:
		_release(action)


func _on_prediction_corrected(acked_frame: int, replayed_ticks: int, position_error: float) -> void:
	corrections.append({
		"frame": acked_frame,
		"replayed": replayed_ticks,
		"error": position_error,
		"step": step,
	})


func _wait_for_pawn() -> void:
	for pawn_root in world.get_pawn_container().get_children():
		var pawn: Pawn = pawn_root.get_node_or_null(^"Pawn")
		if pawn == null or not pawn.is_locally_controlled():
			continue

		var found: CharacterMovementComponent = pawn_root.get_node_or_null(^"CharacterMovementComponent")
		if found == null:
			continue

		var found_backend := found.get_movement_backend() as PredictedMovementBackend
		if found_backend == null:
			_check("the online pawn predicts", "its movement backend is not a PredictedMovementBackend")
			_finish()
			return

		if found_backend.get_role() != PredictedMovementBackend.PREDICTION_ROLE_AUTONOMOUS_PROXY:
			continue
		if found_backend.get_ack_count() == 0:
			continue

		movement = found
		backend = found_backend
		root_3d = pawn_root
		player_input = world.get_local_player(0).get_player_input()
		movement.prediction_corrected.connect(_on_prediction_corrected)

		print("prediction probe: predicting %s, latency %d ms, jitter %d ms, loss %.1f%%" % [
			pawn_root.name,
			ProjectSettings.get_setting("application/game_framework/debug/network_latency_ms", 0),
			ProjectSettings.get_setting("application/game_framework/debug/network_jitter_ms", 0),
			ProjectSettings.get_setting("application/game_framework/debug/network_packet_loss_percent", 0.0)])
		_next(Step.SETTLE)
		return


func _settle() -> void:
	# Whatever the server did before this client's first move arrived is
	# corrected here, and is not what is being tested.
	if step_ticks < 60:
		return

	baseline_corrections = backend.get_correction_count()
	max_ack_error = 0.0
	mark = root_3d.global_position
	_press(&"move_right")
	_next(Step.RESPOND)


func _respond() -> void:
	# The press was picked up by the tick that ran right after it, so two ticks
	# in the character has already moved - a round trip has not happened yet.
	if step_ticks < 3:
		return

	var moved: float = (root_3d.global_position - mark).length()
	_check("moves on the tick the key goes down, not a round trip later",
		"" if moved > 0.01 else "moved %.4f m in 3 ticks" % moved)
	_next(Step.SCRIPT)


func _script() -> void:
	# About three seconds of play with every kind of input the pawn has, ending
	# against the north wall. Nothing here is corrected if prediction is exact.
	match step_ticks:
		40:
			_press(&"jump")
		42:
			_release(&"jump")
			_check("a jump is predicted on the tick it is pressed",
				"" if movement.get_movement_mode() == &"Falling" else "mode is %s" % movement.get_movement_mode())
		70:
			_release(&"move_right")
			_press(&"move_forward")
		100:
			_press(&"dash")
		102:
			_release(&"dash")
			var flat: Vector3 = movement.get_velocity()
			flat.y = 0.0
			_check("a dash sent as a custom input flag is predicted",
				"" if flat.length() > movement.max_walk_speed + 1.0 else "speed %.2f m/s" % flat.length())
		130:
			_release(&"move_forward")
			_press(&"move_left")
		160:
			_release(&"move_left")
			_press(&"move_forward")
		240:
			# Well into the north wall by now: collision is part of what has to agree.
			_release_all()
			_next(Step.DRAIN)


func _drain() -> void:
	# Long enough for every move of the script to be acknowledged.
	if step_ticks < 60:
		return

	var during: int = backend.get_correction_count() - baseline_corrections
	_check("walking, jumping, dashing and hitting a wall need no correction",
		"" if during == 0 else "%d corrections, %s" % [during, _describe(Step.SCRIPT)])
	_check("the server agreed with every predicted tick",
		"" if max_ack_error <= movement.prediction_position_tolerance else "largest disagreement %.4f m" % max_ack_error)
	_check("nothing was left unacknowledged",
		"" if backend.get_pending_move_count() <= _expected_replay_range().y else "%d moves pending" % backend.get_pending_move_count())

	baseline_corrections = backend.get_correction_count()
	mark = root_3d.global_position
	rpc_id(1, &"server_kick")
	_next(Step.KICK_WAIT)


func _kick_wait() -> void:
	for correction in corrections:
		if correction["step"] == Step.KICK_WAIT and correction["error"] > 1.0:
			kick_correction = correction
			break

	if kick_correction.is_empty():
		if step_ticks > 120:
			_check("a change the server made is corrected", "no correction within 2 s")
			_finish()
		return

	var error: float = kick_correction["error"]
	_check("a change the server made is corrected by about its size",
		"" if absf(error - KICK.length()) < 0.2 else "corrected by %.3f m, moved by %.3f m" % [error, KICK.length()])

	var range_ticks: Vector2i = _expected_replay_range()
	var replayed: int = kick_correction["replayed"]
	_check("about a round trip of moves is replayed",
		"" if replayed >= range_ticks.x and replayed <= range_ticks.y else "replayed %d, expected %d to %d" % [replayed, range_ticks.x, range_ticks.y])

	var shift: Vector3 = root_3d.global_position - mark
	_check("the character ends up where the server put it",
		"" if absf(shift.x - KICK.x) < 0.1 else "shifted by %s" % shift)

	baseline_corrections = backend.get_correction_count()
	max_ack_error = 0.0
	_press(&"move_back")
	_next(Step.AFTER)


func _after() -> void:
	if step_ticks == 40:
		_release(&"move_back")

	if step_ticks < 100:
		return

	var after: int = backend.get_correction_count() - baseline_corrections
	_check("one correction is enough - agreement resumes after it",
		"" if after == 0 else "%d more corrections, %s" % [after, _describe(Step.AFTER)])
	_check("and stays exact",
		"" if max_ack_error <= movement.prediction_position_tolerance else "largest disagreement %.4f m" % max_ack_error)
	_finish()


# The first few corrections recorded during one step, which is enough to see
# whether they are one large disagreement or a steady drip of small ones.
func _describe(during: Step) -> String:
	var parts: PackedStringArray = []
	for correction in corrections:
		if correction["step"] != during:
			continue
		parts.append("frame %d by %.3f m" % [correction["frame"], correction["error"]])
		if parts.size() == 4:
			parts.append("...")
			break
	return "first: " + ", ".join(parts)


func _expected_replay_range() -> Vector2i:
	# One way out, the server's input buffer, one way back - in ticks, with the
	# jitter either side and a few ticks for where in a frame things land.
	var latency: float = ProjectSettings.get_setting("application/game_framework/debug/network_latency_ms", 0)
	var jitter: float = ProjectSettings.get_setting("application/game_framework/debug/network_jitter_ms", 0)
	var buffer: int = ProjectSettings.get_setting("application/game_framework/prediction/input_buffer_ticks", 2)
	var tick_ms: float = 1000.0 / Engine.physics_ticks_per_second
	var low: int = maxi(1, floori((2.0 * latency - 2.0 * jitter) / tick_ms))
	var high: int = ceili((2.0 * latency + 2.0 * jitter) / tick_ms) + buffer + 5
	return Vector2i(low, high)


func _finish() -> void:
	if step == Step.DONE:
		return
	step = Step.DONE

	if player_input != null:
		_release_all()

	if backend != null:
		print("prediction probe: %d acks, %d corrections, %d ticks replayed in all" % [
			backend.get_ack_count(), backend.get_correction_count(), backend.get_total_replayed_ticks()])

	if failures.is_empty():
		print("prediction probe: all checks passed")
	else:
		print("prediction probe: %d check(s) failed" % failures.size())
		for failure in failures:
			print("    - " + failure)

	world.quit(0 if failures.is_empty() else 1)
