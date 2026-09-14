extends SceneTree

# The one rule client prediction rests on, checked directly: a tick is
# (state, input, dt) -> state', and nothing else.
#
# Prediction replays ticks. After a correction it puts an earlier state back
# and runs the same inputs through the same code again, and the result has to
# be exactly what the first run produced - otherwise every correction is
# followed by another one. Anything a tick reads that is not in MovementState
# or MovementInput breaks that: the body's own transform, a field kept on the
# component, a layered move shared between the history and the live state.
# None of them fail loudly. They make replay quietly wrong.
#
# So this runs three seconds of walking, jumping, crouching, a layered move with
# state of its own and a wall, records the state and input of every tick, then
# rolls back to several points in the middle and replays - and demands the same
# answer to the last bit, in 3D and in 2D. Each rollback point is replayed twice,
# so a history the first replay consumed is caught too.
#
# Run with:
#   godot --headless --path project --script res://tests/test_replay.gd

const STEP := 1.0 / 60.0
const TICKS := 180
const ROLLBACK_POINTS := [10, 50, 75, 102, 150]
const FLAG_SHOVE := 1

var failures: Array[String] = []
var started := false


# A layered move that counts its own ticks in a stored property. It is exactly
# the kind of move a shallow history gets wrong: replay from before it ended
# and a shared instance would already have counted to zero.
class ShoveMove extends LayeredMove:
	@export_storage var ticks_left := 12
	@export_storage var push := Vector3.ZERO

	func _generate_move(_params: MovementTickParams, out_proposal: ProposedMove) -> void:
		ticks_left -= 1
		out_proposal.linear_velocity = push
		out_proposal.mix_mode = ProposedMove.OVERRIDE_ALL_EXCEPT_VERTICAL

	func _is_finished(_sim_time_ms: float) -> bool:
		return ticks_left <= 0


func _check(name: String, complaint: String) -> void:
	print("    %s %s%s" % ["PASS" if complaint.is_empty() else "FAIL", name,
		"" if complaint.is_empty() else "  -  " + complaint])
	if not complaint.is_empty():
		failures.append("%s: %s" % [name, complaint])


# --- The input script, shared by both dimensions -------------------------------

# Right into the wall, a jump on the way, a crouch and a shove, then away from
# the wall with another jump.
func _drive(i: int, pawn: Pawn, cmc: Node, right: Vector3, away: Vector3) -> void:
	pawn.add_movement_input(right if i < 120 else away)

	if i == 20 or i == 140:
		cmc.jump()
	if i == 60:
		cmc.crouch()
	if i == 90:
		cmc.un_crouch()
	if i == 45 or i == 100:
		cmc.add_custom_input_flags(FLAG_SHOVE)


# --- Record, roll back, replay -------------------------------------------------

func _record(cmc: Node, pawn: Pawn, right: Vector3, away: Vector3) -> Dictionary:
	var inputs: Array[MovementInput] = []
	var states: Array[MovementState] = []

	for i in TICKS:
		_drive(i, pawn, cmc, right, away)
		cmc.simulate(STEP)
		inputs.append(cmc.get_input().duplicate_input())
		states.append(cmc.get_state().duplicate_state(true))

	return { "inputs": inputs, "states": states }


func _replay_from(cmc: Node, record: Dictionary, from_tick: int) -> String:
	var inputs: Array[MovementInput] = record["inputs"]
	var states: Array[MovementState] = record["states"]

	# The same copy the prediction backend makes before it rolls back, so the
	# history is never handed to the live simulation to consume.
	cmc.rollback_to_state(states[from_tick].duplicate_state(true))

	for i in range(from_tick + 1, TICKS):
		cmc.simulate_with_input(inputs[i], STEP)

		var now: MovementState = cmc.get_state()
		var then: MovementState = states[i]
		var position_error: float = now.position.distance_to(then.position)
		var velocity_error: float = now.velocity.distance_to(then.velocity)

		if position_error > 1e-5 or velocity_error > 1e-4 or now.movement_mode != then.movement_mode \
				or now.is_crouching != then.is_crouching:
			return "tick %d: position off by %.6f, velocity off by %.6f, mode %s vs %s, crouching %s vs %s" % [
				i, position_error, velocity_error, now.movement_mode, then.movement_mode,
				now.is_crouching, then.is_crouching]

	return ""


func _verify(label: String, cmc: Node, record: Dictionary, sanity: String) -> void:
	_check("%s: the run did what the script asked" % label, sanity)

	for from_tick in ROLLBACK_POINTS:
		var first: String = _replay_from(cmc, record, from_tick)
		var second: String = _replay_from(cmc, record, from_tick)
		_check("%s: replaying from tick %d reproduces every later tick" % [label, from_tick], first)
		_check("%s: and again from the same history" % label, second)


# --- 3D -------------------------------------------------------------------------

func _build_3d() -> Dictionary:
	var ground := StaticBody3D.new()
	var ground_shape := CollisionShape3D.new()
	var ground_box := BoxShape3D.new()
	ground_box.size = Vector3(60.0, 1.0, 60.0)
	ground_shape.shape = ground_box
	ground.add_child(ground_shape)
	ground.position = Vector3(0.0, -0.5, 0.0)
	root.add_child(ground)

	var wall := StaticBody3D.new()
	var wall_shape := CollisionShape3D.new()
	var wall_box := BoxShape3D.new()
	wall_box.size = Vector3(1.0, 4.0, 20.0)
	wall_shape.shape = wall_box
	wall.add_child(wall_shape)
	wall.position = Vector3(6.5, 2.0, 0.0)
	root.add_child(wall)

	var body := CharacterBody3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.4
	capsule.height = 1.8
	var shape := CollisionShape3D.new()
	shape.shape = capsule
	body.add_child(shape)
	body.position = Vector3(0.0, 0.9, 0.0)
	root.add_child(body)

	var pawn := Pawn.new()
	pawn.replicate_transform = false
	body.add_child(pawn)

	var cmc := CharacterMovementComponent.new()
	cmc.replicate_movement_mode = false
	body.add_child(cmc)

	cmc.custom_input_flags.connect(func(flags: int) -> void:
		if flags & FLAG_SHOVE:
			var shove := ShoveMove.new()
			shove.push = Vector3(9.0, 0.0, 3.0)
			cmc.queue_layered_move(shove))

	return { "body": body, "pawn": pawn, "cmc": cmc }


# --- 2D -------------------------------------------------------------------------

func _build_2d() -> Dictionary:
	# Far from the 3D scene's origin is irrelevant - 2D and 3D physics are
	# separate worlds - but its own floor and wall are needed.
	var ground := StaticBody2D.new()
	var ground_shape := CollisionShape2D.new()
	var ground_box := RectangleShape2D.new()
	ground_box.size = Vector2(6000.0, 100.0)
	ground_shape.shape = ground_box
	ground.add_child(ground_shape)
	ground.position = Vector2(0.0, 50.0)
	root.add_child(ground)

	var wall := StaticBody2D.new()
	var wall_shape := CollisionShape2D.new()
	var wall_box := RectangleShape2D.new()
	wall_box.size = Vector2(100.0, 400.0)
	wall_shape.shape = wall_box
	wall.add_child(wall_shape)
	wall.position = Vector2(300.0, -200.0)
	root.add_child(wall)

	var body := CharacterBody2D.new()
	var capsule := CapsuleShape2D.new()
	capsule.radius = 20.0
	capsule.height = 90.0
	var shape := CollisionShape2D.new()
	shape.shape = capsule
	body.add_child(shape)
	body.position = Vector2(0.0, -45.0)
	root.add_child(body)

	var pawn := Pawn.new()
	pawn.replicate_transform = false
	body.add_child(pawn)

	var cmc := CharacterMovementComponent2D.new()
	body.add_child(cmc)

	cmc.custom_input_flags.connect(func(flags: int) -> void:
		if flags & FLAG_SHOVE:
			var shove := ShoveMove.new()
			# Away from the wall: into it, the wall would hide whether the shove
			# was replayed at all.
			shove.push = Vector3(-700.0, 0.0, 0.0)
			cmc.queue_layered_move(shove))

	return { "body": body, "pawn": pawn, "cmc": cmc }


var scene_3d: Dictionary
var scene_2d: Dictionary


func _initialize() -> void:
	scene_3d = _build_3d()
	scene_2d = _build_2d()


func _physics_process(_delta: float) -> bool:
	# One frame, so the bodies are in their spaces before anything is swept.
	if not started:
		started = true
		return false

	_run_3d()
	_run_2d()

	if failures.is_empty():
		print("test_replay: all checks passed")
		quit(0)
	else:
		print("test_replay: %d check(s) failed" % failures.size())
		quit(1)
	return true


func _run_3d() -> void:
	print("=== 3D ===")
	var cmc: CharacterMovementComponent = scene_3d["cmc"]
	var record: Dictionary = _record(cmc, scene_3d["pawn"], Vector3(1, 0, 0), Vector3(-1, 0, -1))

	var states: Array[MovementState] = record["states"]
	var modes := {}
	var crouched := false
	var shoved := false
	for state in states:
		modes[state.movement_mode] = true
		crouched = crouched or state.is_crouching
		shoved = shoved or state.get_active_layered_moves().size() > 0
	var at_wall: float = states[119].position.x

	var sanity := ""
	if not modes.has(&"Falling"):
		sanity = "never jumped"
	elif not crouched:
		sanity = "never crouched"
	elif not shoved:
		sanity = "the shove never ran"
	elif absf(at_wall - (6.0 - 0.4)) > 0.05:
		sanity = "stopped at x = %.3f, not against the wall" % at_wall

	_verify("3D", cmc, record, sanity)


func _run_2d() -> void:
	print("=== 2D ===")
	var cmc: CharacterMovementComponent2D = scene_2d["cmc"]
	var record: Dictionary = _record(cmc, scene_2d["pawn"], Vector3(1, 0, 0), Vector3(-1, 0, 0))

	var states: Array[MovementState] = record["states"]
	var modes := {}
	var crouched := false
	var shoved := false
	for state in states:
		modes[state.movement_mode] = true
		crouched = crouched or state.is_crouching
		shoved = shoved or state.get_active_layered_moves().size() > 0
	var at_wall := -INF
	for state in states:
		at_wall = maxf(at_wall, state.position.x)

	var sanity := ""
	if not modes.has(&"Falling"):
		sanity = "never jumped"
	elif not crouched:
		sanity = "never crouched"
	elif not shoved:
		sanity = "the shove never ran"
	elif absf(at_wall - (250.0 - 20.0)) > 2.0:
		sanity = "got no further than x = %.1f, not to the wall" % at_wall

	_verify("2D", cmc, record, sanity)
