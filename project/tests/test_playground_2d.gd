extends SceneTree

# The 2D playground, station by station.
#
# The map is only worth having if walking right still does what each sign says,
# and a block moved by 40 px can quietly turn a gap into a wall or a step into a
# cliff. So each station gets its own drop-in and its own two seconds, rather
# than one long run that the first gap ends.
#
# The level scene is instantiated as-is, so this checks the real geometry. There
# is no World here, which is why the level exposes attach_to(): in a real run the
# game mode calls it from player_restarted.
#
# Run with:
#   godot --headless --path project --script res://tests/test_playground_2d.gd

var level: Node
var body: CharacterBody2D
var cmc: CharacterMovementComponent2D
var pawn: Pawn

var frames := 0
var station := 0
var modes: Array[String] = []
var jumped := false
var failures: Array[String] = []

# name, start, frames, input, crouch, jump_at, check(x, y, mode, ground) -> String.
# The check returns "" when the station behaved, or why it did not.
var plan: Array = []


func _check(name: String, complaint: String) -> void:
	print("    %s %s%s" % ["PASS" if complaint.is_empty() else "FAIL", name,
		"" if complaint.is_empty() else "  -  " + complaint])
	if not complaint.is_empty():
		failures.append("%s: %s" % [name, complaint])


func _initialize() -> void:
	level = load("res://levels/test_level_2d.tscn").instantiate()
	root.add_child(level)

	body = CharacterBody2D.new()
	var capsule := CapsuleShape2D.new()
	capsule.radius = 16.0
	capsule.height = 64.0
	var shape := CollisionShape2D.new()
	shape.shape = capsule
	body.add_child(shape)
	root.add_child(body)

	pawn = Pawn.new()
	pawn.replicate_transform = false
	body.add_child(pawn)

	cmc = CharacterMovementComponent2D.new()
	body.add_child(cmc)
	cmc.movement_mode_changed.connect(func(_from, to): modes.append(to))

	plan = [
		["1  flat ground reaches max_walk_speed", Vector2(100, -40), 60, Vector2(1, 0), false, -1.0,
			func(_x, _y, mode, _g): return "" if mode == &"Walking" and absf(cmc.get_velocity().x - 600.0) < 5.0 \
				else "mode=%s vx=%.0f, wanted Walking at 600" % [mode, cmc.get_velocity().x]],

		["2  a 60 px step blocks, being over max_step_height", Vector2(700, -40), 120, Vector2(1, 0), false, -1.0,
			func(x, y, _m, _g): return "" if absf(x - 964.0) < 2.0 and absf(y + 94.0) < 5.0 \
				else "stopped at (%.1f, %.1f), wanted (964, -94)" % [x, y]],

		["2  and the same step is cleared by a jump", Vector2(700, -40), 210, Vector2(1, 0), false, 950.0,
			func(x, _y, _m, _g): return "" if x > 1300.0 else "only reached x=%.0f, wanted past 1300" % x],

		["3  a 30 deg ramp is walked up at full speed", Vector2(1460, -40), 45, Vector2(1, 0), false, -1.0,
			func(_x, y, mode, _g): return "" if mode == &"Walking" and y < -150.0 \
				else "mode=%s y=%.0f, wanted Walking above -150" % [mode, y]],

		["3  a 60 deg ramp cannot be walked up", Vector2(2050, -220), 150, Vector2(1, 0), false, -1.0,
			func(x, _y, _m, _g): return "" if absf(x - 2218.0) < 6.0 \
				else "ended at x=%.0f, wanted to be stopped near 2218" % x],

		["4  a 300 px gap is cleared with a run-up", Vector2(2420, -40), 110, Vector2(1, 0), false, 2660.0,
			func(x, _y, _m, ground): return "" if x > 3000.0 and ground \
				else "ended at x=%.0f ground=%s, wanted past 3000 and landed" % [x, ground]],

		["5  the tunnel blocks a standing capsule", Vector2(3320, -40), 100, Vector2(1, 0), false, -1.0,
			func(x, _y, _m, _g): return "" if absf(x - 3434.0) < 2.0 \
				else "stopped at x=%.1f, wanted 3434" % x],

		["5  and lets a crouched one through", Vector2(3320, -40), 130, Vector2(1, 1), true, -1.0,
			func(x, _y, _m, _g): return "" if x > 3800.0 and cmc.is_crouching() \
				else "ended at x=%.0f crouched=%s, wanted past 3800 while crouched" % [x, cmc.is_crouching()]],

		["6  the shuttle carries a still character", Vector2.ZERO, 150, Vector2.ZERO, false, -1.0,
			func(x, _y, _m, ground): return "" if ground and absf(x - level.get_node("Shuttle").position.x) < 40.0 \
				else "at x=%.0f ground=%s, shuttle at %.0f" % [x, ground, level.get_node("Shuttle").position.x]],

		["7  walking into the pool starts swimming", Vector2(5050, -40), 180, Vector2(1, 0), false, -1.0,
			func(_x, _y, mode, _g): return "" if mode == &"Swimming" else "mode=%s, wanted Swimming" % mode],

		["8  the updraft switches to flying and lifts", Vector2(6150, -40), 120, Vector2(0, -1), false, -1.0,
			func(_x, y, _m, _g): return "" if modes.has("Flying") and y < -400.0 \
				else "modes=%s y=%.0f, wanted Flying and a climb past -400" % [modes, y]],
	]

	# What the game mode's player_restarted does in a real run.
	level.attach_to(cmc)
	print("=== the 2D playground, station by station ===")
	_start()


func _start() -> void:
	frames = 0
	jumped = false
	modes.clear()

	# Station 6 begins on the shuttle, wherever it happens to have got to.
	if station == 8:
		body.position = level.get_node("Shuttle").position + Vector2(0.0, -50.0)
	else:
		body.position = plan[station][1]

	cmc.set_velocity(Vector2.ZERO)
	cmc.set_movement_mode(&"Walking")
	cmc.cancel_all_layered_moves()
	pawn.consume_movement_input_vector()


func _physics_process(_delta: float) -> bool:
	frames += 1

	# Two warm-up frames: the level's own _ready builds the geometry.
	if frames < 3:
		return false

	var row: Array = plan[station]
	var input: Vector2 = row[3]
	pawn.add_movement_input(Vector3(input.x, input.y, 0.0))

	if row[4] and cmc.is_on_ground():
		cmc.crouch()
	else:
		cmc.un_crouch()

	var jump_at: float = row[5]
	if jump_at > 0.0 and not jumped and body.position.x >= jump_at and cmc.is_on_ground():
		cmc.jump()
		jumped = true

	if frames < int(row[2]):
		return false

	var check: Callable = row[6]
	_check(row[0], check.call(body.position.x, body.position.y, cmc.get_movement_mode(), cmc.is_on_ground()))

	station += 1
	if station >= plan.size():
		return _finish()

	_start()
	return false


func _finish() -> bool:
	if failures.is_empty():
		print("test_playground_2d: all checks passed")
	else:
		print("test_playground_2d: %d FAILED" % failures.size())
		for failure in failures:
			print("  - %s" % failure)
		quit(1)
	return true
