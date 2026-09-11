extends SceneTree

# Perching, in both of its halves.
#
# perch_radius_threshold cuts both ways, and both halves are checked here.
#
# It is mostly a restriction on standing: it narrows the footprint that has to
# have ground under it, so a character whose contact is out near the capsule's
# rim loses its floor and falls instead of hanging off the edge. The threshold
# defaults to 0, which is Unreal's default too and switches the whole test off.
#
# The other half gives standing back, and it is much narrower. Reading
# find_floor_basic, it needs three things at once: the wide sweep hits something
# unwalkable, the axis ray finds nothing walkable (a line_trace result makes
# find_floor skip perching entirely), and a narrow sweep from the same transform
# does find ground. A slot in the floor thinner than the perch footprint, with a
# steep kerb beside it, is such a place.
#
# Run with:
#   godot --headless --path project --script res://tests/test_perch.gd
#
# The ledge is a slab from x = -4 to x = 0 with its top face at y = 0. The capsule
# is radius 0.3, half height 0.9, so at y = 0.92 its feet sit 0.02 above the slab -
# inside the band find_floor() works in.

const LEDGE_EDGE := 0.0
const BODY_Y := 0.92
const CAPSULE_RADIUS := 0.3

## The slot rig, parked well clear of the ledge.
const SLOT_X := 100.0

# The scan step, plus room for the sweep margin.
const TOLERANCE := 0.015

var body: CharacterBody3D
var cmc: CharacterMovementComponent
var pawn: Pawn
var frames := 0
var phase := 0
var failures: Array[String] = []
var walk_x := {}

func _initialize() -> void:
	var slab := StaticBody3D.new()
	var slab_shape := CollisionShape3D.new()
	var slab_box := BoxShape3D.new()
	slab_box.size = Vector3(4.0, 1.0, 4.0)
	slab_shape.shape = slab_box
	slab.add_child(slab_shape)
	slab.position = Vector3(-2.0, -0.5, 0.0)
	root.add_child(slab)

	_build_slot_rig()

	body = CharacterBody3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.radius = CAPSULE_RADIUS
	capsule.height = 1.8
	var body_shape := CollisionShape3D.new()
	body_shape.shape = capsule
	body.add_child(body_shape)
	body.position = Vector3(-1.0, BODY_Y, 0.0)
	root.add_child(body)

	pawn = Pawn.new()
	pawn.replicate_transform = false
	body.add_child(pawn)

	cmc = CharacterMovementComponent.new()
	body.add_child(cmc)

# A 0.16 m slot with flat lips, and a 60 degree kerb starting at x + 0.172. The
# wide capsule's hemisphere meets the kerb's face about 0.009 above it - closer
# than the 0.031 it clears the lips by - so the sweep calls the floor unwalkable,
# and the axis ray drops straight through the slot. A 0.11 footprint rests on the
# lips.
func _build_slot_rig() -> void:
	for offset in [-5.0, 0.08]:
		var slab := StaticBody3D.new()
		var shape := CollisionShape3D.new()
		var box := BoxShape3D.new()
		box.size = Vector3(4.92, 0.2, 20.0)
		shape.shape = box
		slab.add_child(shape)
		slab.position = Vector3(SLOT_X + offset + 2.46, -0.1, 0.0)
		root.add_child(slab)

	var kerb := StaticBody3D.new()
	var kerb_shape := CollisionShape3D.new()
	var hull := ConvexPolygonShape3D.new()
	hull.points = PackedVector3Array([
		Vector3(0.172, 0.0, -1.0), Vector3(0.5, 0.568, -1.0), Vector3(0.5, 0.0, -1.0),
		Vector3(0.172, 0.0, 1.0), Vector3(0.5, 0.568, 1.0), Vector3(0.5, 0.0, 1.0),
	])
	kerb_shape.shape = hull
	kerb.add_child(kerb_shape)
	kerb.position = Vector3(SLOT_X, 0.0, 0.0)
	root.add_child(kerb)


func _check(condition: bool, message: String) -> void:
	print("    %s %s" % ["PASS" if condition else "FAIL", message])
	if not condition:
		failures.append(message)

# The footprint the perch test stands on, mirroring get_valid_perch_radius().
func _perch_footprint(threshold: float) -> float:
	if threshold <= 0.0:
		return CAPSULE_RADIUS
	return clampf(CAPSULE_RADIUS - threshold, 0.11, CAPSULE_RADIUS)

# The furthest past the edge the solver still calls this a walkable floor.
func _last_standable_x(threshold: float) -> float:
	cmc.perch_radius_threshold = threshold
	var last := -1.0
	var x := -0.5
	while x <= 0.6:
		if cmc.find_floor_at(Transform3D(Basis(), Vector3(x, BODY_Y, 0.0))).is_walkable_floor():
			last = x
		x += 0.005
	return last

func _reset() -> void:
	body.position = Vector3(-1.0, BODY_Y, 0.0)
	cmc.set_velocity(Vector3.ZERO)
	cmc.set_movement_mode(&"Walking")
	pawn.consume_movement_input_vector()

func _physics_process(_delta: float) -> bool:
	frames += 1

	# One warm-up frame, so _ready has resolved the body and read the capsule.
	if frames < 2:
		return false

	if phase == 0:
		print("=== the standable overhang equals the perch footprint ===")
		for threshold in [0.0, 0.1, 0.2]:
			var expected := _perch_footprint(threshold)
			var actual := _last_standable_x(threshold) - LEDGE_EDGE
			_check(absf(actual - expected) <= TOLERANCE,
				"threshold %.2f: last standable x = %+.3f, footprint %.2f" % [threshold, actual, expected])

		print("=== over a slot too thin for the footprint, perching gives standing back ===")
		var slot_transform := Transform3D(Basis(), Vector3(SLOT_X, BODY_Y, 0.0))

		cmc.perch_radius_threshold = 0.0
		var without := cmc.find_floor_at(slot_transform)
		_check(not without.is_walkable_floor(),
			"perching off: the 60 degree kerb is all the wide capsule finds, walkable=%s" % without.is_walkable_floor())

		cmc.perch_radius_threshold = 0.2
		var with_perch := cmc.find_floor_at(slot_transform)
		_check(with_perch.is_walkable_floor() and with_perch.get_line_trace(),
			"perching on: the narrow footprint finds the lips, walkable=%s line_trace=%s" % [
				with_perch.is_walkable_floor(), with_perch.get_line_trace()])

		print("=== walking off the ledge leaves it sooner with perching on ===")
		cmc.perch_radius_threshold = 0.0
		_reset()
		phase = 1
		return false

	pawn.add_movement_input(Vector3(1.0, 0.0, 0.0))

	if cmc.is_falling():
		walk_x[phase] = body.position.x
		print("    perching %-9s left the ledge at x = %+.3f" % [
			"off" if phase == 1 else "on", body.position.x])

		if phase == 1:
			cmc.perch_radius_threshold = 0.2
			_reset()
			phase = 2
			return false

		# The gap is wide because a walking character overshoots the static cut-off
		# by however far it travels in one tick; only the ordering is asserted.
		_check(walk_x[2] < walk_x[1] - 0.1,
			"perched %+.3f is clearly short of unperched %+.3f" % [walk_x[2], walk_x[1]])
		return _finish()

	if frames > 600:
		_check(false, "the character never left the ledge")
		return _finish()

	return false

func _finish() -> bool:
	if failures.is_empty():
		print("test_perch: all checks passed")
	else:
		print("test_perch: %d FAILED" % failures.size())
		for f in failures:
			print("  - %s" % f)
		quit(1)
	return true
