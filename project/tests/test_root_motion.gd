extends SceneTree

# Root motion, in both dimensions.
#
# RootMotionLayeredMove is a LayeredMove and nothing more: the animation proposes
# a velocity, the active mode still does the sweeping, the collision and the
# floor. So the checks here are as much about what root motion may NOT do - pass
# through a wall, hang in the air - as about the speed it drives.
#
# Godot accumulates root motion only from TYPE_POSITION_3D tracks. A 2D pawn
# therefore needs a plain Node3D hung under it as the carrier for the track to
# address; a 2D position track contributes nothing at all. That is what the 2D
# half of this file is really pinning down.
#
# Run with:
#   godot --headless --path project --script res://tests/test_root_motion.gd

const AUTHORED_SPEED := 120.0
const TOLERANCE := 3.0

var failures: Array[String] = []
var frames := 0
var stage := 0

var body_3d: CharacterBody3D
var cmc_3d: CharacterMovementComponent
var pawn_3d: Pawn
var player_3d: AnimationPlayer
var rm_3d: RootMotionLayeredMove

var body_2d: CharacterBody2D
var cmc_2d: CharacterMovementComponent2D
var pawn_2d: Pawn
var player_2d: AnimationPlayer
var rm_2d: RootMotionLayeredMove

func _check(condition: bool, message: String) -> void:
	print("    %s %s" % ["PASS" if condition else "FAIL", message])
	if not condition:
		failures.append(message)

# One animation, one TYPE_POSITION_3D track, walking +x at AUTHORED_SPEED.
func _build_player(parent: Node, distance: float) -> AnimationPlayer:
	var carrier := Node3D.new()
	carrier.name = "RootMotion"
	parent.add_child(carrier)

	var anim := Animation.new()
	anim.length = 1.0
	anim.loop_mode = Animation.LOOP_LINEAR
	var track := anim.add_track(Animation.TYPE_POSITION_3D)
	anim.track_set_path(track, NodePath("RootMotion"))
	anim.position_track_insert_key(track, 0.0, Vector3.ZERO)
	anim.position_track_insert_key(track, 1.0, Vector3(distance, 0.0, 0.0))

	var lib := AnimationLibrary.new()
	lib.add_animation("run", anim)

	var player := AnimationPlayer.new()
	player.name = "AnimationPlayer"
	parent.add_child(player)
	player.add_animation_library("", lib)
	player.callback_mode_process = AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_PHYSICS
	player.root_node = NodePath("..")
	player.root_motion_track = NodePath("RootMotion")
	return player

func _initialize() -> void:
	# --- 3D: ground at y = 0, wall face at x = 2.0 -------------------------------
	var ground_3d := StaticBody3D.new()
	var g3s := CollisionShape3D.new()
	var g3b := BoxShape3D.new()
	g3b.size = Vector3(80.0, 1.0, 80.0)
	g3s.shape = g3b
	ground_3d.add_child(g3s)
	ground_3d.position = Vector3(0.0, -0.5, 0.0)
	root.add_child(ground_3d)

	var wall_3d := StaticBody3D.new()
	var w3s := CollisionShape3D.new()
	var w3b := BoxShape3D.new()
	w3b.size = Vector3(1.0, 4.0, 8.0)
	w3s.shape = w3b
	wall_3d.add_child(w3s)
	wall_3d.position = Vector3(2.5, 2.0, 0.0)
	root.add_child(wall_3d)

	body_3d = CharacterBody3D.new()
	var c3 := CapsuleShape3D.new()
	c3.radius = 0.3
	c3.height = 1.8
	var b3s := CollisionShape3D.new()
	b3s.shape = c3
	body_3d.add_child(b3s)
	body_3d.position = Vector3(0.0, 0.92, 0.0)
	root.add_child(body_3d)

	# 1.2 m/s in 3D is the same animation as 120 px/s in 2D.
	player_3d = _build_player(body_3d, AUTHORED_SPEED * 0.01)

	pawn_3d = Pawn.new()
	pawn_3d.replicate_transform = false
	body_3d.add_child(pawn_3d)

	cmc_3d = CharacterMovementComponent.new()
	cmc_3d.orient_rotation_to_movement = false
	body_3d.add_child(cmc_3d)

	rm_3d = RootMotionLayeredMove.new()
	rm_3d.animation_mixer_path = NodePath("AnimationPlayer")

	# --- 2D: ground top at y = 0, wall face at x = 180 ---------------------------
	var ground_2d := StaticBody2D.new()
	var g2s := CollisionShape2D.new()
	var g2r := RectangleShape2D.new()
	g2r.size = Vector2(8000.0, 100.0)
	g2s.shape = g2r
	ground_2d.add_child(g2s)
	ground_2d.position = Vector2(0.0, 50.0)
	root.add_child(ground_2d)

	var wall_2d := StaticBody2D.new()
	var w2s := CollisionShape2D.new()
	var w2r := RectangleShape2D.new()
	w2r.size = Vector2(40.0, 400.0)
	w2s.shape = w2r
	wall_2d.add_child(w2s)
	wall_2d.position = Vector2(200.0, -200.0)
	root.add_child(wall_2d)

	body_2d = CharacterBody2D.new()
	var c2 := CapsuleShape2D.new()
	c2.radius = 16.0
	c2.height = 64.0
	var b2s := CollisionShape2D.new()
	b2s.shape = c2
	body_2d.add_child(b2s)
	body_2d.position = Vector2(0.0, -34.0)
	root.add_child(body_2d)

	player_2d = _build_player(body_2d, AUTHORED_SPEED)

	pawn_2d = Pawn.new()
	pawn_2d.replicate_transform = false
	body_2d.add_child(pawn_2d)

	cmc_2d = CharacterMovementComponent2D.new()
	body_2d.add_child(cmc_2d)

	rm_2d = RootMotionLayeredMove.new()
	rm_2d.animation_mixer_path = NodePath("AnimationPlayer")

func _restart_3d(at: Vector3, basis: Basis, playing: bool) -> void:
	frames = 0

	# The body, not the state: moving the body trips simulate()'s teleport check,
	# which adopts the body's whole transform - so a rotation written to the state
	# here would be overwritten by the body's on the very next tick.
	body_3d.transform = Transform3D(basis, at)
	cmc_3d.set_velocity(Vector3.ZERO)
	cmc_3d.set_movement_mode(&"Walking")
	cmc_3d.cancel_all_layered_moves()
	pawn_3d.consume_movement_input_vector()
	if playing:
		player_3d.play("run")
		player_3d.seek(0.0, true)
		cmc_3d.queue_layered_move(rm_3d)
	else:
		player_3d.stop()

func _restart_2d(at: Vector2, playing: bool) -> void:
	frames = 0
	body_2d.position = at
	cmc_2d.set_velocity(Vector2.ZERO)
	cmc_2d.set_movement_mode(&"Walking")
	cmc_2d.cancel_all_layered_moves()
	pawn_2d.consume_movement_input_vector()
	if playing:
		player_2d.play("run")
		player_2d.seek(0.0, true)
		cmc_2d.queue_layered_move(rm_2d)
	else:
		player_2d.stop()

func _park() -> void:
	# Out of the way, and not running, while the other dimension is measured.
	player_3d.stop()
	player_2d.stop()
	cmc_3d.cancel_all_layered_moves()
	cmc_2d.cancel_all_layered_moves()

func _physics_process(_delta: float) -> bool:
	frames += 1

	# One warm-up frame, so _ready has resolved both components.
	if frames < 2 and stage == 0:
		return false

	match stage:
		0:
			print("=== 3D: the animation is the only thing moving it ===")
			_park()
			_restart_3d(Vector3(0.0, 0.92, 0.0), Basis(), true)
			stage = 1
		1:
			if frames == 60:
				var moved := body_3d.position.x
				_check(absf(moved - 1.2) <= TOLERANCE * 0.01,
					"3D: moved %.3f m in 1 s, animation authors 1.200" % moved)
				print("=== 3D: facing turns the step - the same animation, yawed 90 degrees ===")
				_park()
				_restart_3d(Vector3(0.0, 0.92, 0.0), Basis.from_euler(Vector3(0.0, PI * 0.5, 0.0)), true)
				stage = 2
		2:
			if frames == 60:
				# Yawing +90 degrees about Y sends local +x to world -z.
				var p := body_3d.position
				_check(absf(p.z + 1.2) <= TOLERANCE * 0.01 and absf(p.x) <= TOLERANCE * 0.01,
					"3D: yawed 90 deg went to z=%.3f (expected -1.200), x=%.3f" % [p.z, p.x])
				print("=== 3D: a wall still stops it ===")
				_park()
				_restart_3d(Vector3(0.0, 0.92, 0.0), Basis(), true)
				stage = 3
		3:
			if frames == 180:
				# Wall face at x = 2.0, capsule radius 0.3, so the stop is at 1.7.
				_check(absf(body_3d.position.x - 1.7) <= 0.05,
					"3D: stopped at x=%.3f, wall face 2.000 less the 0.300 radius" % body_3d.position.x)
				print("=== 2D: the animation is the only thing moving it ===")
				_park()
				_restart_2d(Vector2(0.0, -34.0), true)
				stage = 4
		4:
			if frames == 60:
				_check(absf(body_2d.position.x - AUTHORED_SPEED) <= TOLERANCE,
					"2D: moved %.1f px in 1 s, animation authors %.1f" % [body_2d.position.x, AUTHORED_SPEED])
				print("=== 2D: nothing moves without the move queued ===")
				_park()
				_restart_2d(Vector2(0.0, -34.0), false)
				stage = 5
		5:
			if frames == 60:
				_check(absf(body_2d.position.x) < 0.01,
					"2D: moved %.1f px with no move and no input" % body_2d.position.x)
				print("=== 2D: a wall still stops it ===")
				_park()
				_restart_2d(Vector2(0.0, -34.0), true)
				stage = 6
		6:
			if frames == 120:
				# Wall face at x = 180, capsule radius 16, so the stop is at 164.
				_check(absf(body_2d.position.x - 164.0) <= 1.0,
					"2D: stopped at x=%.1f, wall face 180 less the 16 radius" % body_2d.position.x)
				print("=== 2D: vertical is left to gravity ===")
				_park()
				_restart_2d(Vector2(0.0, -600.0), true)
				cmc_2d.set_movement_mode(&"Falling")
				stage = 7
		7:
			if frames == 30:
				var fell := body_2d.position.y - (-600.0)
				_check(absf(body_2d.position.x - AUTHORED_SPEED * 0.5) <= TOLERANCE and fell > 50.0,
					"2D: in the air the animation drove x=%.1f while gravity dropped it %.1f px" % [
						body_2d.position.x, fell])
				return _finish()

	if frames > 400:
		_check(false, "stage %d never finished" % stage)
		return _finish()

	return false

func _finish() -> bool:
	if failures.is_empty():
		print("test_root_motion: all checks passed")
	else:
		print("test_root_motion: %d FAILED" % failures.size())
		for f in failures:
			print("  - %s" % f)
		quit(1)
	return true
