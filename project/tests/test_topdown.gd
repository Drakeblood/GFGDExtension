extends SceneTree

# The top-down playground, and with it every claim the movement reference's
# "Top-down 2D" section makes.
#
# There is no gravity axis on a top-down screen, so there is no floor, so walking
# has nothing to stand on. FlyingMode2D is the top-down mover: no gravity, no
# floor test, input not flattened, plain collide-and-slide. What this file pins
# down is that it really behaves that way - the same speed in all eight
# directions with nothing pulling sideways, walls that stop at exactly the
# capsule radius and slide rather than grab, and the two settings that quietly
# ruin it if they are left the way a side-on game wants them.
#
# Run with:
#   godot --headless --path project --script res://tests/test_topdown.gd

# Open floor, far enough from the west wall and the pillars that a half second
# run in any of eight directions meets nothing.
const START := Vector2(400.0, 1000.0)
const STEP := 1.0 / 60.0

var level: Node
var body: CharacterBody2D
var cmc: CharacterMovementComponent2D
var pawn: Pawn
var failures: Array[String] = []
var started := false


func _check(name: String, complaint: String) -> void:
	print("    %s %s%s" % ["PASS" if complaint.is_empty() else "FAIL", name,
		"" if complaint.is_empty() else "  -  " + complaint])
	if not complaint.is_empty():
		failures.append("%s: %s" % [name, complaint])


func _initialize() -> void:
	level = load("res://levels/test_level_topdown.tscn").instantiate()
	root.add_child(level)

	body = CharacterBody2D.new()
	var capsule := CapsuleShape2D.new()
	capsule.radius = 16.0
	capsule.height = 32.0
	var shape := CollisionShape2D.new()
	shape.shape = capsule
	body.add_child(shape)
	root.add_child(body)

	pawn = Pawn.new()
	pawn.replicate_transform = false
	body.add_child(pawn)

	cmc = CharacterMovementComponent2D.new()
	cmc.gravity = 0.0
	cmc.starting_mode = &"Flying"
	cmc.can_crouch = false
	body.add_child(cmc)


func _reset(at: Vector2 = START) -> void:
	body.position = at
	cmc.set_velocity(Vector2.ZERO)
	cmc.cancel_all_layered_moves()
	cmc.set_movement_mode(&"Flying")
	pawn.consume_movement_input_vector()


func _run(direction: Vector2, seconds: float) -> void:
	for i in int(seconds * 60.0):
		pawn.add_movement_input(Vector3(direction.x, direction.y, 0.0))
		cmc.simulate(STEP)


func _physics_process(_delta: float) -> bool:
	# One frame, so the level's _ready has built the room.
	if not started:
		started = true
		return false

	print("=== the top-down playground ===")

	# --- nothing pulls you anywhere, and every direction is the same ----------
	var distances: Array[float] = []
	var speeds: Array[float] = []
	for direction in [
		Vector2(1, 0), Vector2(-1, 0), Vector2(0, -1), Vector2(0, 1),
		Vector2(1, -1), Vector2(1, 1), Vector2(-1, -1), Vector2(-1, 1),
	]:
		_reset()
		_run(direction, 0.5)
		distances.append((body.position - START).length())
		speeds.append(cmc.get_velocity().length())

	var spread: float = distances.max() - distances.min()
	_check("all eight directions travel the same distance",
		"" if spread < 1.0 else "spread of %.1f px between %.1f and %.1f" % [
			spread, distances.min(), distances.max()])

	_check("and all reach max_fly_speed, so diagonals get no bonus",
		"" if absf(speeds.min() - cmc.max_fly_speed) < 1.0 and absf(speeds.max() - cmc.max_fly_speed) < 1.0 \
			else "speeds ran %.1f to %.1f, wanted %.0f" % [speeds.min(), speeds.max(), cmc.max_fly_speed])

	_reset()
	_run(Vector2.ZERO, 1.0)
	_check("standing still stays still - there is no gravity to drift under",
		"" if (body.position - START).length() < 0.01 else "drifted %s" % [body.position - START])

	# --- walls ----------------------------------------------------------------
	_reset(Vector2(300.0, 400.0))
	_run(Vector2(1, 0), 2.0)
	_check("a pillar stops the capsule at exactly its radius",
		"" if absf(body.position.x - 684.0) < 0.5 else "stopped at x=%.1f, pillar face is 700 less the 16 radius" % body.position.x)

	# Into the closed part of the corridor wall, pushing north-east: the wall
	# takes the east and leaves the north.
	_reset(Vector2(1000.0, 400.0))
	_run(Vector2(1, -1), 1.0)
	_check("pushing diagonally into a wall slides along it",
		"" if absf(body.position.x - 1164.0) < 1.0 and body.position.y < 300.0 \
			else "ended at (%.1f, %.1f), wanted to be held at x=1164 and slid north" % [body.position.x, body.position.y])

	# Aimed at the gap, it goes through.
	_reset(Vector2(1000.0, 800.0))
	_run(Vector2(1, 0), 1.5)
	_check("the corridor gap lets it through",
		"" if body.position.x > 1300.0 else "only reached x=%.1f, wanted past 1300" % body.position.x)

	# A 45 degree face: pushing straight east must deflect, not stop dead.
	# Aimed at the wedge's upper face, which should push it north.
	_reset(Vector2(1300.0, 600.0))
	_run(Vector2(1, 0), 1.5)
	_check("a 45 degree wall deflects rather than blocking",
		"" if body.position.y < 550.0 and body.position.x > 1450.0 \
			else "ended at (%.1f, %.1f), wanted to be pushed north off the line y=600" % [body.position.x, body.position.y])

	# --- the mud --------------------------------------------------------------
	# East of the wedge, so the run starts on open floor rather than inside it.
	_reset(Vector2(1840.0, 800.0))
	_run(Vector2(1, 0), 0.7)
	var in_mud: bool = cmc.get_movement_mode() == &"Swimming"
	_check("the mud switches to Swimming",
		"" if in_mud else "mode=%s" % cmc.get_movement_mode())

	var before_drift := body.position
	_run(Vector2.ZERO, 1.0)
	var drift: Vector2 = body.position - before_drift
	_check("and with gravity at 0 it is a pure current, carrying a still character",
		"" if drift.y > 150.0 and absf(drift.x) < 60.0 \
			else "drifted %s in a second, wanted about (0, 220)" % [drift])

	# --- the two settings a side-on game gets right and this one must not -----
	_check("can_crouch is off on the pawn scene",
		"" if not _pawn_scene_crouches() else "the top-down pawn scene leaves can_crouch on, which lurches it 16 px sideways")

	for entry in [
		[ProposedMove.OVERRIDE_VELOCITY, true, "OVERRIDE_VELOCITY"],
		[ProposedMove.OVERRIDE_ALL_EXCEPT_VERTICAL, false, "OVERRIDE_ALL_EXCEPT_VERTICAL"],
	]:
		_reset()
		var dash := LinearVelocityLayeredMove.new()
		dash.velocity = Vector3(0.0, 1200.0, 0.0)
		dash.duration = 0.4
		dash.mix_mode = entry[0]
		cmc.queue_layered_move(dash)
		_run(Vector2.ZERO, 0.4)

		var moved: float = absf(body.position.y - START.y)
		var worked: bool = moved > 400.0
		_check("a southward dash with %s %s" % [entry[2], "moves" if entry[1] else "does nothing, which is why the pawn avoids it"],
			"" if worked == entry[1] else "moved %.1f px" % moved)

	return _finish()


func _pawn_scene_crouches() -> bool:
	var scene: PackedScene = load("res://scenes/player_pawn_topdown.tscn")
	var instance: Node = scene.instantiate()
	var movement: CharacterMovementComponent2D = instance.get_node_or_null(^"CharacterMovementComponent2D")
	var crouches: bool = movement != null and movement.can_crouch
	instance.free()
	return crouches


func _finish() -> bool:
	if failures.is_empty():
		print("test_topdown: all checks passed")
	else:
		print("test_topdown: %d FAILED" % failures.size())
		for failure in failures:
			print("  - %s" % failure)
		quit(1)
	return true
