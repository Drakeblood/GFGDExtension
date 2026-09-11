extends Level
## The 3D movement playground: one station per thing CharacterMovementComponent
## does, laid out along +X so walking forward is the tour.
##
## The geometry is built here from the tables below rather than authored as a
## hundred nodes in the .tscn, because a row of numbers is easier to read and to
## move than a CollisionShape3D with a sub-resource. Edit SLABS, RAMPS and SIGNS
## and the map changes.
##
## Units are metres and up is +Y. A slab row is [centre, size, colour]; a ramp is given by
## the segment its top face runs along, which is the only part of a ramp anyone
## cares about. The walkway is 10 m wide, z from -5 to 5, and where a station has
## something you cannot get past it is put off to one side so the tour never dead
## ends.

const GROUND := Color(0.20, 0.22, 0.26)
const PROP := Color(0.32, 0.35, 0.40)
const BLOCKER := Color(0.44, 0.27, 0.27)
const PLATFORM := Color(0.34, 0.42, 0.30)
const WATER := Color(0.16, 0.38, 0.52, 0.45)
const UPDRAFT := Color(0.42, 0.36, 0.16, 0.28)

## Anything below this has fallen off the map.
const RESPAWN_BELOW := -12.0

## The column that turns falling into flying. Read by the transition as plain
## geometry, so nothing in a tick has to look at the scene.
const UPDRAFT_MIN := Vector3(126.0, -1.0, -3.0)
const UPDRAFT_MAX := Vector3(130.0, 11.0, 3.0)

const SLABS: Array = [
	# 1 - walking. Flat, and long enough to reach max_walk_speed (6 m/s).
	[Vector3(0.0, -0.5, 0.0), Vector3(24.0, 1.0, 10.0), GROUND],

	# 2 - steps. 0.2 and 0.4 are under max_step_height (0.45) and are walked up;
	# the third rise is 0.6 and has to be jumped. All of it sits on the left half
	# of the walkway, so the right half is a way round.
	[Vector3(20.0, -0.5, 0.0), Vector3(16.0, 1.0, 10.0), GROUND],
	[Vector3(14.0, 0.1, -2.5), Vector3(2.0, 0.2, 5.0), PROP],
	[Vector3(16.0, 0.3, -2.5), Vector3(2.0, 0.6, 5.0), PROP],
	[Vector3(18.0, 0.6, -2.5), Vector3(2.0, 1.2, 5.0), BLOCKER],
	[Vector3(21.0, 0.6, -2.5), Vector3(4.0, 1.2, 5.0), PROP],

	# 3 - slopes. The 30 degree ramp climbs to this plateau; the 60 degree one
	# beside it does not.
	[Vector3(40.0, -0.5, 0.0), Vector3(24.0, 1.0, 10.0), GROUND],
	[Vector3(37.0, 1.0, -2.5), Vector3(4.0, 2.0, 5.0), PROP],

	# 4 - a 3 m gap to fall into, and to jump. A jump carries about 5 m.
	[Vector3(52.5, -0.5, 0.0), Vector3(9.0, 1.0, 10.0), GROUND],
	[Vector3(66.0, -0.5, 0.0), Vector3(12.0, 1.0, 10.0), GROUND],

	# 5 - the crouch tunnel. Its underside is 1.2 m up; a standing capsule is 2 m
	# tall and a crouched one 1 m. It spans the full width on purpose.
	[Vector3(78.0, -0.5, 0.0), Vector3(12.0, 1.0, 10.0), GROUND],
	[Vector3(78.0, 2.1, 0.0), Vector3(6.0, 1.8, 10.0), BLOCKER],

	# 6 - moving platforms. Ground either side of the shuttle's gap, and the ledge
	# the lift reaches.
	[Vector3(87.0, -0.5, 0.0), Vector3(6.0, 1.0, 10.0), GROUND],
	[Vector3(104.0, -0.5, 0.0), Vector3(12.0, 1.0, 10.0), GROUND],
	[Vector3(105.0, 4.0, 0.0), Vector3(6.0, 0.4, 10.0), PROP],

	# 7 - water. A pit with the surface level with the ground, so you walk
	# straight in and jump straight out.
	[Vector3(110.5, -0.5, 0.0), Vector3(1.0, 1.0, 10.0), GROUND],
	[Vector3(121.5, -0.5, 0.0), Vector3(1.0, 1.0, 10.0), GROUND],
	[Vector3(116.0, -4.5, 0.0), Vector3(10.0, 1.0, 10.0), GROUND],
	[Vector3(116.0, -2.0, -5.5), Vector3(10.0, 6.0, 1.0), GROUND],
	[Vector3(116.0, -2.0, 5.5), Vector3(10.0, 6.0, 1.0), GROUND],

	# 8 - the updraft, and the shelf only flying reaches.
	[Vector3(130.0, -0.5, 0.0), Vector3(16.0, 1.0, 10.0), GROUND],
	[Vector3(133.0, 6.0, 0.0), Vector3(6.0, 0.4, 10.0), PROP],
]

## Ramps, given as the segment their top face runs along in the XY plane, plus
## how deep and where across the walkway: [from, to, z_centre, z_size, colour].
const RAMPS: Array = [
	# 30 degrees: walkable, so the velocity bends along it and the character
	# climbs at full speed.
	[Vector2(32.0, 0.0), Vector2(35.0, 2.0), -2.5, 5.0, PROP],
	[Vector2(39.0, 2.0), Vector2(42.0, 0.0), -2.5, 5.0, PROP],

	# 60 degrees: over walkable_floor_angle (45), so it cannot be climbed at all.
	# Off to the right, so failing to climb it costs nothing.
	[Vector2(34.0, 0.0), Vector2(34.9, 1.6), 2.5, 5.0, BLOCKER],
	[Vector2(37.1, 1.6), Vector2(38.0, 0.0), 2.5, 5.0, BLOCKER],
	[Vector2(34.9, 1.6), Vector2(37.1, 1.6), 2.5, 5.0, BLOCKER],
]

const SIGNS: Array = [
	[Vector3(-8.0, 2.6, -4.0), "1 - Walking\nWASD, up to 6 m/s"],
	[Vector3(12.5, 3.2, -4.0), "2 - Steps\n0.2 and 0.4 m are walked up.\nThe next rise is 0.6, over\nmax_step_height (0.45).\nJump it, or go round right."],
	[Vector3(30.5, 4.2, -4.0), "3 - Slopes\nLeft 30 deg: walkable.\nRight 60 deg: not.\nThe limit is 45."],
	[Vector3(51.0, 2.6, -4.0), "4 - Gap\n3 m. A jump carries 5.\nWalk off to fall instead."],
	[Vector3(72.5, 3.6, -4.0), "5 - Crouch\nHold C. The gap is 1.2 m;\nstanding is 2, crouched 1."],
	[Vector3(84.5, 3.2, -4.0), "6 - Platforms\nRide across, then take the\nlift. Stepping off a moving\none keeps its momentum."],
	[Vector3(107.5, 2.6, -4.0), "7 - Water\nWalk in to swim.\nSpace jumps out."],
	[Vector3(123.0, 4.2, -4.0), "8 - Flying\nThe updraft switches the\nmode. Leave it and you fall.\nThe shelf is only reachable\nthat way."],
	[Vector3(-8.0, 1.2, 4.0), "F dashes.\nE is the ability demo."],
]

var _shuttle: AnimatableBody3D
var _lift: AnimatableBody3D
var _elapsed := 0.0

var _hud: Label
var _movement: CharacterMovementComponent
var _start_position := Vector3.ZERO


func _ready() -> void:
	print("GFGD demo:      [%s] _ready" % name)

	for row in SLABS:
		_add_slab(row[0], row[1], row[2])

	for row in RAMPS:
		_add_ramp(row[0], row[1], row[2], row[3], row[4])

	for row in SIGNS:
		_add_sign(row[0], row[1])

	_shuttle = _add_platform("Shuttle", Vector3(93.0, -0.2, 0.0), Vector3(4.0, 0.4, 8.0))
	_lift = _add_platform("Lift", Vector3(100.0, -0.2, 0.0), Vector3(4.0, 0.4, 6.0))

	_add_water(Vector3(116.0, -2.0, 0.0), Vector3(10.0, 4.0, 10.0))
	_add_updraft()

	_hud = get_node_or_null(^"UI/HUD")
	var start: Node3D = get_node_or_null(^"PlayerStart")
	if start != null:
		_start_position = start.position


func _init_level(world: World) -> void:
	print("GFGD demo: >> %s._init_level" % name)

	# The first pawn is spawned inside the game mode's init_game, which has already
	# run by the time a level gets this hook - so that first player_restarted is
	# long gone. Take the pawn that is there, and keep the signal for respawns.
	var game_mode: GameModeBase = world.get_game_mode()
	if game_mode != null:
		game_mode.player_restarted.connect(_on_player_restarted)

	var controller: PlayerController = world.get_first_player_controller()
	if controller != null:
		_on_player_restarted(controller, controller.get_pawn())


func _on_player_restarted(_controller: PlayerController, pawn: Pawn) -> void:
	if pawn == null:
		return

	attach_to(pawn.get_pawn_root().get_node_or_null(^"CharacterMovementComponent"))


## Takes over a movement component: caches it for the HUD and gives it the
## updraft. Public so a headless test can drive the map without a World.
func attach_to(movement: CharacterMovementComponent) -> void:
	if movement == null or movement == _movement:
		return

	_movement = movement
	print("GFGD demo: >> %s took over the pawn's CharacterMovementComponent" % name)

	var transitions: Array = movement.transitions

	# The component registers a WaterMovementTransition of its own, but only if
	# transitions is still empty when its _ready runs - clearing the list is the
	# documented way to opt out of paying for the point query. So appending before
	# that happens would take the water away without a word. Put one in if it is
	# not there, and this works whichever order the two run in.
	var has_water := false
	for transition in transitions:
		if transition is WaterMovementTransition:
			has_water = true
			break

	if not has_water:
		transitions.append(WaterMovementTransition.new())

	# The updraft is a transition rather than something a mode knows about, which
	# is the whole argument for transitions being objects: neither falling nor
	# flying has to have heard of it.
	var fly := UpdraftTransition.new()
	fly.zone = AABB(UPDRAFT_MIN, UPDRAFT_MAX - UPDRAFT_MIN)
	transitions.append(fly)

	movement.transitions = transitions


func _physics_process(delta: float) -> void:
	_elapsed += delta

	# Both platforms are driven from here rather than from an AnimationPlayer so
	# their speed is readable as a number. sync_to_physics is what turns that
	# motion into a collider velocity the component can read.
	if _shuttle != null:
		_shuttle.position.x = 93.0 + 3.0 * (1.0 - cos(_elapsed * 0.8))

	# Its low point is -0.2, where the top face is 0.2 m up and so still inside
	# max_step_height (0.45) - otherwise there would be no way onto it from the
	# ground. Its high point puts that face level with the ledge at 4.2.
	if _lift != null:
		_lift.position.y = -0.2 + 2.1 * (1.0 - cos(_elapsed * 0.7))


func _process(_delta: float) -> void:
	if _hud == null or _movement == null or not is_instance_valid(_movement):
		return

	var body: Node3D = _movement.get_updated_body()
	if body == null:
		return

	if body.position.y < RESPAWN_BELOW:
		body.position = _start_position
		_movement.set_velocity(Vector3.ZERO)

	var floor_result: FloorResult = _movement.get_current_floor()
	var velocity: Vector3 = _movement.get_velocity()

	_hud.text = "\n".join([
		"mode      %s" % _movement.get_movement_mode(),
		"position  (%.1f, %.1f, %.1f)" % [body.position.x, body.position.y, body.position.z],
		"velocity  (%.1f, %.1f, %.1f)   speed %.1f" % [velocity.x, velocity.y, velocity.z, velocity.length()],
		"ground    %s   crouched %s   height %.2f" % [
			_movement.is_on_ground(), _movement.is_crouching(), _movement.get_capsule_half_height()],
		"floor     walkable=%s dist=%.3f normal=(%.2f, %.2f, %.2f)" % [
			floor_result.is_walkable_floor(), floor_result.get_distance_to_floor(),
			floor_result.get_normal().x, floor_result.get_normal().y, floor_result.get_normal().z],
		"immersion %.2f   base %s" % [
			_movement.get_immersion_depth_at(Transform3D(Basis(), body.position)),
			_movement.get_state().has_base()],
		"",
		"WASD move   Space jump   C crouch   F dash   E ability demo",
	])


# --- Building ----------------------------------------------------------------

func _material(colour: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = colour
	return material


func _add_slab(centre: Vector3, size: Vector3, colour: Color) -> void:
	var body := StaticBody3D.new()
	body.position = centre

	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = size
	shape.shape = box
	body.add_child(shape)

	var visual := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	visual.mesh = mesh
	visual.material_override = _material(colour)
	body.add_child(visual)

	add_child(body)


## Given the segment a ramp's top face runs along, works out where the box behind
## it goes. Which is the way round anyone actually thinks about a ramp.
func _add_ramp(from: Vector2, to: Vector2, z_centre: float, z_size: float, colour: Color) -> void:
	const THICKNESS := 1.0

	var along: Vector2 = to - from
	var length: float = along.length()
	if length < 0.001:
		return

	var angle: float = along.angle()
	var normal := Vector2(-along.y, along.x).normalized()
	var centre_2d: Vector2 = (from + to) * 0.5 - normal * (THICKNESS * 0.5)

	var body := StaticBody3D.new()
	body.position = Vector3(centre_2d.x, centre_2d.y, z_centre)
	body.rotation = Vector3(0.0, 0.0, angle)

	var size := Vector3(length, THICKNESS, z_size)

	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = size
	shape.shape = box
	body.add_child(shape)

	var visual := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	visual.mesh = mesh
	visual.material_override = _material(colour)
	body.add_child(visual)

	add_child(body)


func _add_platform(node_name: String, centre: Vector3, size: Vector3) -> AnimatableBody3D:
	var body := AnimatableBody3D.new()
	body.name = node_name

	# Without this the physics server never learns the platform's velocity, and
	# stepping off one would not carry its momentum with you.
	body.sync_to_physics = true
	body.position = centre

	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = size
	shape.shape = box
	body.add_child(shape)

	var visual := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	visual.mesh = mesh
	visual.material_override = _material(PLATFORM)
	body.add_child(visual)

	add_child(body)
	return body


func _add_water(centre: Vector3, size: Vector3) -> void:
	var volume := WaterVolume.new()
	volume.name = "Pool"
	volume.position = centre

	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = size
	shape.shape = box
	volume.add_child(shape)

	var visual := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	visual.mesh = mesh
	var material := _material(WATER)
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	visual.material_override = material
	volume.add_child(visual)

	add_child(volume)


func _add_updraft() -> void:
	# Only a marker. The switch to Flying is the transition's doing, and it reads
	# the box rather than this node - a transition that queried the scene could
	# not be replayed.
	var size: Vector3 = UPDRAFT_MAX - UPDRAFT_MIN
	var visual := MeshInstance3D.new()
	visual.position = UPDRAFT_MIN + size * 0.5
	var mesh := BoxMesh.new()
	mesh.size = size
	visual.mesh = mesh
	var material := _material(UPDRAFT)
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	visual.material_override = material
	add_child(visual)


func _add_sign(at: Vector3, text: String) -> void:
	var label := Label3D.new()
	label.position = at
	label.text = text
	label.font_size = 64

	# 0.009 m per pixel puts a line at about 0.6 m tall, which is readable from the
	# camera's 9 m without the longest line running off the walkway.
	label.pixel_size = 0.009
	label.billboard = BaseMaterial3D.BILLBOARD_FIXED_Y
	label.modulate = Color(0.92, 0.93, 0.96)

	# An outline instead of no_depth_test. Drawing through the geometry made the
	# signs for stations you cannot see yet float over the ones you are standing
	# in; being occluded like everything else is the whole point of putting them
	# in the world.
	label.outline_size = 20
	label.outline_modulate = Color(0.04, 0.05, 0.07, 0.9)
	add_child(label)


# --- The updraft -------------------------------------------------------------

class UpdraftTransition extends MovementModeTransition:
	## Inside the box the character flies; outside it, it falls again.
	##
	## Geometry, not a node query: everything this reads comes from the state
	## being simulated, which is what lets a replayed tick reach the same answer.
	var zone: AABB

	func _evaluate(params: MovementTickParams) -> StringName:
		var state: MovementState = params.get_out_state()
		var inside: bool = zone.has_point(state.position)
		var flying: bool = state.movement_mode == &"Flying"

		if inside and not flying:
			return &"Flying"

		if flying and not inside:
			return &"Falling"

		return &""
