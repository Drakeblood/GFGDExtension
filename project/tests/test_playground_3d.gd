extends SceneTree

# The 3D playground, station by station.
#
# Same idea as the 2D one: the map is only worth having if walking forward still
# does what each sign says, and a slab moved by half a metre can quietly turn a
# gap into a floor or a step into a cliff. Each station gets its own drop-in and
# its own couple of seconds.
#
# The level scene is instantiated as-is, so this checks the real geometry. There
# is no World here, which is why the level exposes attach_to(): in a real run the
# game mode calls it from player_restarted.
#
# Run with:
#   godot --headless --path project --script res://tests/test_playground_3d.gd

const STEP := 1.0 / 60.0

var level: Node
var body: CharacterBody3D
var cmc: CharacterMovementComponent
var pawn: Pawn

var frames := 0
var station := 0
var modes: Array[String] = []
var jumped := false
var failures: Array[String] = []

## A second pawn, parked far off the map, purely so the camera it carries can be
## read at the end. Built here rather than at the end because its camera is aimed
## from _process, and that needs frames to have passed.
var probe_pawn: Node3D

# name, start, seconds, input, crouch, jump_at_x, check -> String ("" means good)
var plan: Array = []


func _check(name: String, complaint: String) -> void:
	print("    %s %s%s" % ["PASS" if complaint.is_empty() else "FAIL", name,
		"" if complaint.is_empty() else "  -  " + complaint])
	if not complaint.is_empty():
		failures.append("%s: %s" % [name, complaint])


func _initialize() -> void:
	level = load("res://levels/test_level.tscn").instantiate()
	root.add_child(level)

	body = CharacterBody3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.radius = 0.5
	capsule.height = 2.0
	var shape := CollisionShape3D.new()
	shape.shape = capsule
	body.add_child(shape)
	root.add_child(body)

	pawn = Pawn.new()
	pawn.replicate_transform = false
	body.add_child(pawn)

	cmc = CharacterMovementComponent.new()
	body.add_child(cmc)
	cmc.movement_mode_changed.connect(func(_from, to): modes.append(to))

	plan = [
		["1  flat ground reaches max_walk_speed", Vector3(-8, 1.02, 0), 1.5, Vector3(1, 0, 0), false, -1.0,
			func(p, mode, _g): return "" if mode == &"Walking" and absf(cmc.get_velocity().length() - 6.0) < 0.2 \
				else "mode=%s speed=%.2f at %s, wanted Walking at 6" % [mode, cmc.get_velocity().length(), p]],

		["2  0.2 and 0.4 m steps are walked up", Vector3(12, 1.02, -2.5), 1.2, Vector3(1, 0, 0), false, -1.0,
			func(p, _m, _g): return "" if p.y > 1.4 else "reached y=%.2f, wanted to be up the 0.6 m of steps" % p.y],

		["2  a 0.6 m rise blocks, being over max_step_height", Vector3(12, 1.02, -2.5), 2.5, Vector3(1, 0, 0), false, -1.0,
			func(p, _m, _g): return "" if absf(p.x - 16.5) < 0.15 else "stopped at x=%.2f, wanted 16.5" % p.x],

		["2  and the same rise is cleared by a jump", Vector3(12, 1.02, -2.5), 2.2, Vector3(1, 0, 0), false, 16.0,
			func(p, _m, ground): return "" if p.x > 18.0 and p.y > 2.0 and ground \
				else "ended at %s ground=%s, wanted past x=18 up on the 1.2 m platform" % [p, ground]],

		["3  a 30 degree ramp is walked up", Vector3(30, 1.02, -2.5), 1.6, Vector3(1, 0, 0), false, -1.0,
			func(p, mode, _g): return "" if mode == &"Walking" and p.y > 2.0 \
				else "mode=%s y=%.2f, wanted Walking above 2.0" % [mode, p.y]],

		["3  a 60 degree ramp cannot be walked up", Vector3(31, 1.02, 2.5), 2.5, Vector3(1, 0, 0), false, -1.0,
			func(p, _m, _g): return "" if p.x < 35.0 and p.y < 2.0 \
				else "ended at %s, wanted to be held below the ramp's 1.6 m top" % p],

		["4  a 3 m gap is cleared with a run-up", Vector3(50, 1.02, 0), 2.6, Vector3(1, 0, 0), false, 56.0,
			func(p, _m, ground): return "" if p.x > 60.5 and ground \
				else "ended at %s ground=%s, wanted past x=60.5 and landed" % [p, ground]],

		["4  and walking off it falls", Vector3(50, 1.02, 0), 2.0, Vector3(1, 0, 0), false, -1.0,
			func(p, mode, _g): return "" if mode == &"Falling" and p.y < 0.0 \
				else "mode=%s y=%.2f, wanted to be falling below 0" % [mode, p.y]],

		["5  the tunnel blocks a standing capsule", Vector3(72, 1.02, 0), 2.0, Vector3(1, 0, 0), false, -1.0,
			func(p, _m, _g): return "" if absf(p.x - 74.5) < 0.15 else "stopped at x=%.2f, wanted 74.5" % p.x],

		["5  and lets a crouched one through", Vector3(72, 1.02, 0), 3.5, Vector3(1, 0, 0), true, -1.0,
			func(p, _m, _g): return "" if p.x > 81.5 and cmc.is_crouching() \
				else "ended at x=%.2f crouched=%s, wanted past 81.5 while crouched" % [p.x, cmc.is_crouching()]],

		["6  the shuttle carries a still character", Vector3.ZERO, 2.0, Vector3.ZERO, false, -1.0,
			func(p, _m, ground): return "" if ground and absf(p.x - level.get_node("Shuttle").position.x) < 1.5 \
				else "at x=%.2f ground=%s, shuttle at %.2f" % [p.x, ground, level.get_node("Shuttle").position.x]],

		["7  walking into the pool starts swimming", Vector3(107, 1.02, 0), 3.0, Vector3(1, 0, 0), false, -1.0,
			func(_p, mode, _g): return "" if mode == &"Swimming" else "mode=%s, wanted Swimming" % mode],

		["8  the updraft switches to flying and lifts", Vector3(128, 1.02, 0), 2.0, Vector3(0, 1, 0), false, -1.0,
			func(p, _m, _g): return "" if modes.has("Flying") and p.y > 5.0 \
				else "modes=%s y=%.2f, wanted Flying and a climb past 5" % [modes, p.y]],
	]

	probe_pawn = load("res://scenes/player_pawn.tscn").instantiate()
	probe_pawn.position = Vector3(-300.0, 1.0, 0.0)
	root.add_child(probe_pawn)

	# What the game mode's player_restarted does in a real run.
	level.attach_to(cmc)
	print("=== the 3D playground, station by station ===")
	_start()


func _start() -> void:
	frames = 0
	jumped = false
	modes.clear()

	# Station 6 begins on the shuttle, wherever it happens to have got to.
	if station == 10:
		body.position = level.get_node("Shuttle").position + Vector3(0.0, 1.3, 0.0)
	else:
		body.position = plan[station][1]

	cmc.set_velocity(Vector3.ZERO)
	cmc.set_movement_mode(&"Walking")
	cmc.cancel_all_layered_moves()
	pawn.consume_movement_input_vector()


func _physics_process(_delta: float) -> bool:
	frames += 1

	# Two warm-up frames: the level's own _ready builds the geometry.
	if frames < 3:
		return false

	var row: Array = plan[station]
	var input: Vector3 = row[3]
	pawn.add_movement_input(input)

	if row[4] and cmc.is_on_ground():
		cmc.crouch()
	else:
		cmc.un_crouch()

	var jump_at: float = row[5]
	if jump_at > 0.0 and not jumped and body.position.x >= jump_at and cmc.is_on_ground():
		cmc.jump()
		jumped = true

	if frames < int(row[2] * 60.0):
		return false

	var check: Callable = row[6]
	_check(row[0], check.call(body.position, cmc.get_movement_mode(), cmc.is_on_ground()))

	station += 1
	if station >= plan.size():
		_check_presentation()
		return _finish()

	_start()
	return false


# The two things that make the map walkable rather than merely correct, and that
# an edit to player_pawn.tscn can silently undo.
func _check_presentation() -> void:
	print("=== the view ===")

	var pawn_root: Node3D = probe_pawn

	var camera: Camera3D = pawn_root.get_node_or_null("Camera")
	if camera == null:
		_check("the pawn has a camera", "no Camera node under player_pawn.tscn")
		return

	_check("the camera is detached from the character's rotation",
		"" if camera.top_level else "top_level is off, so orienting to movement swings the view")

	var forward: Vector3 = (-camera.global_basis.z).slide(Vector3.UP).normalized()
	_check("the camera looks down the map, so W walks along it",
		"" if forward.distance_to(Vector3.RIGHT) < 0.05 \
			else "forward is %s, wanted +X - input is taken relative to this" % forward)

	var signs: Array = level.find_children("*", "Label3D", true, false)
	_check("the level has signs", "" if not signs.is_empty() else "no Label3D found")

	var through_walls := 0
	for sign in signs:
		if sign.no_depth_test:
			through_walls += 1

	_check("the signs are occluded like everything else",
		"" if through_walls == 0 else "%d of %d draw through the geometry" % [through_walls, signs.size()])


func _finish() -> bool:
	if failures.is_empty():
		print("test_playground_3d: all checks passed")
	else:
		print("test_playground_3d: %d FAILED" % failures.size())
		for failure in failures:
			print("  - %s" % failure)
		quit(1)
	return true
