extends Level
## The 2D playground: one station per thing CharacterMovementComponent2D does,
## laid out left to right so walking right is the tour.
##
## The geometry is built here from the tables below rather than authored as forty
## nodes in the .tscn, because a row of four numbers is easier to read and to move
## than a CollisionShape2D with a sub-resource. Edit BLOCKS, RAMPS and SIGNS and
## the map changes; nothing here is magic.
##
## Coordinates are pixels and y grows downward, so the ground's top face is y = 0
## and everything you can stand on is at a negative y. A block row is
## [x, y, width, height] with (x, y) the top-left corner.

const GROUND := Color(0.20, 0.22, 0.26)
const PROP := Color(0.30, 0.33, 0.38)
const BLOCKER := Color(0.42, 0.26, 0.26)
const PLATFORM := Color(0.34, 0.40, 0.30)
const WATER := Color(0.16, 0.35, 0.48, 0.45)
const UPDRAFT := Color(0.40, 0.34, 0.16, 0.30)

## Anything below this has fallen off the map.
const RESPAWN_BELOW := 520.0

## The column that turns falling into flying. Read by the transition as plain
## geometry, so nothing in a tick has to look at the scene.
const UPDRAFT_ZONE := Rect2(6100.0, -520.0, 300.0, 520.0)

# --- The map -----------------------------------------------------------------

const BLOCKS: Array = [
	# Station 1 - walking. Flat, and long enough to reach max_walk_speed.
	[-240.0, 0.0, 880.0, 240.0, GROUND],

	# Station 2 - steps. 20 and 40 are under max_step_height (45) and are walked
	# up; 60 is over it and has to be jumped.
	[640.0, 0.0, 800.0, 240.0, GROUND],
	[780.0, -20.0, 100.0, 20.0, PROP],
	[880.0, -60.0, 100.0, 60.0, PROP],
	[980.0, -120.0, 100.0, 120.0, BLOCKER],
	[1080.0, -120.0, 220.0, 120.0, PROP],

	# Station 3 - slopes. The plateau between the two ramps, and enough ground
	# after the steep ramp to build speed again before the gap.
	[1440.0, 0.0, 960.0, 240.0, GROUND],
	[1800.0, -173.0, 200.0, 173.0, PROP],

	# Station 4 - a gap to fall into, and to jump.
	[2400.0, 0.0, 300.0, 240.0, GROUND],
	[3000.0, 0.0, 300.0, 240.0, GROUND],

	# Station 5 - the crouch tunnel. The ceiling's underside is 40 px up; a
	# standing capsule is 64 tall and a crouched one 32.
	[3300.0, 0.0, 600.0, 240.0, GROUND],
	[3450.0, -160.0, 340.0, 120.0, BLOCKER],

	# Station 6 - moving platforms. Ground either side of the shuttle's gap, and
	# a ledge the lift reaches.
	[3900.0, 0.0, 200.0, 240.0, GROUND],
	[4500.0, 0.0, 600.0, 240.0, GROUND],
	[4730.0, -320.0, 330.0, 40.0, PROP],

	# Station 7 - water. A pit with the waterline level with the ground, so you
	# walk straight in and jump straight out.
	[5100.0, 0.0, 60.0, 400.0, GROUND],
	[5760.0, 0.0, 60.0, 400.0, GROUND],
	[5160.0, 360.0, 600.0, 80.0, GROUND],

	# Station 8 - the updraft, and the shelf only flying reaches.
	[5820.0, 0.0, 700.0, 240.0, GROUND],
	[6200.0, -420.0, 320.0, 40.0, PROP],
]

## Ramps, as triangles and trapezoids: [origin_x, origin_y, [points...], colour].
const RAMPS: Array = [
	# 30 degrees: walkable, so it is climbed and the velocity bends along it.
	[1500.0, 0.0, [Vector2(0, 0), Vector2(300, -173), Vector2(300, 0)], PROP],
	[2000.0, -173.0, [Vector2(0, 0), Vector2(200, 173), Vector2(0, 173)], PROP],

	# 60 degrees: over walkable_floor_angle (45), so it cannot be walked up - but
	# its flat top can be jumped onto.
	[2220.0, 0.0, [Vector2(0, 0), Vector2(46, -80), Vector2(126, -80), Vector2(172, 0)], BLOCKER],
]

const SIGNS: Array = [
	[-200.0, -140.0, "1 - Walking\nA / D move. Hold to reach max_walk_speed (600 px/s)."],
	[700.0, -260.0, "2 - Steps\n20 and 40 px are walked up, 60 is over max_step_height (45) - jump it."],
	[1460.0, -300.0, "3 - Slopes\n30 deg is walkable. 60 deg is not, but you can jump onto its top."],
	[2410.0, -180.0, "4 - Gap\nWalk off to fall. Run and jump to clear it - falling off the map respawns you."],
	[3310.0, -260.0, "5 - Crouch\nHold S. The gap is 40 px; standing is 64, crouched is 32."],
	[3910.0, -260.0, "6 - Platforms\nRide the shuttle across. The lift carries you to the ledge."],
	[5160.0, -160.0, "7 - Water\nWalk in to swim. W and S swim up and down, Space jumps out.\nThere is a current running right."],
	[5830.0, -300.0, "8 - Flying\nThe updraft switches the mode to Flying - W and S fly.\nLeave it and you fall."],
]

# --- Runtime -----------------------------------------------------------------

var _shuttle: AnimatableBody2D
var _lift: AnimatableBody2D
var _elapsed := 0.0

var _hud: Label
var _movement: CharacterMovementComponent2D
var _start_position := Vector2.ZERO


func _ready() -> void:
	print("GFGD demo:      [%s] _ready" % name)

	for row in BLOCKS:
		_add_block(row[0], row[1], row[2], row[3], row[4])

	for row in RAMPS:
		_add_ramp(row[0], row[1], row[2], row[3])

	for row in SIGNS:
		_add_sign(row[0], row[1], row[2])

	_shuttle = _add_platform("Shuttle", 4190.0, -20.0, 180.0, 30.0)
	_lift = _add_platform("Lift", 4650.0, -20.0, 160.0, 30.0)

	_add_water(5160.0, 0.0, 600.0, 360.0)
	_add_updraft(UPDRAFT_ZONE)

	_hud = get_node_or_null(^"UI/HUD")
	var start: Node2D = get_node_or_null(^"PlayerStart")
	if start != null:
		_start_position = start.position


func _init_level(world: World) -> void:
	print("GFGD demo: >> %s._init_level" % name)

	# Wired on the spawn rather than polled for: the game mode says when there is
	# a pawn, so there is no frame where the HUD is reading a component that is
	# not there yet and no per-frame search for one.
	#
	# The signal alone is not enough, though. World::start_level runs the game
	# mode's init_game - which is where the first pawn is spawned - before it gets
	# here, so that first player_restarted has already been and gone. Take the
	# pawn that exists now, and keep the signal for respawns.
	var game_mode: GameModeBase = world.get_game_mode()
	if game_mode != null:
		game_mode.player_restarted.connect(_on_player_restarted)

	var controller: PlayerController = world.get_first_player_controller()
	if controller != null:
		_on_player_restarted(controller, controller.get_pawn())


func _on_player_restarted(_controller: PlayerController, pawn: Pawn) -> void:
	if pawn == null:
		return

	attach_to(pawn.get_pawn_root().get_node_or_null(^"CharacterMovementComponent2D"))


## Takes over a movement component: caches it for the HUD and gives it the
## updraft. Public so a headless test can drive the map without a World.
func attach_to(movement: CharacterMovementComponent2D) -> void:
	if movement == null or movement == _movement:
		return

	_movement = movement
	print("GFGD demo: >> %s took over the pawn's CharacterMovementComponent2D" % name)

	var transitions: Array = movement.transitions

	# The component registers a WaterMovementTransition2D of its own, but only if
	# transitions is still empty when its _ready runs - clearing the list is the
	# documented way to opt out of paying for the point query. So appending before
	# that happens would take the water away without a word. Put one in if it is
	# not there, and this works whichever order the two run in.
	var has_water := false
	for transition in transitions:
		if transition is WaterMovementTransition2D:
			has_water = true
			break

	if not has_water:
		transitions.append(WaterMovementTransition2D.new())

	# The updraft is a transition rather than something a mode knows about, which
	# is the whole argument for transitions being objects: neither falling nor
	# flying has to have heard of it.
	var fly := FlyZoneTransition.new()
	fly.zone = UPDRAFT_ZONE
	transitions.append(fly)

	movement.transitions = transitions


func _physics_process(delta: float) -> void:
	_elapsed += delta

	# Both platforms are driven from here rather than from an AnimationPlayer so
	# their speed is readable as a number. sync_to_physics is what turns that
	# motion into a collider velocity the component can read.
	if _shuttle != null:
		_shuttle.position.x = 4190.0 + 110.0 * (1.0 - cos(_elapsed * 1.1))

	# Down to -20, where its top is 35 px up and so still inside max_step_height
	# (45) - otherwise there would be no way onto it from the ground.
	if _lift != null:
		_lift.position.y = -20.0 - 150.0 * (1.0 - cos(_elapsed * 0.9))


func _process(_delta: float) -> void:
	if _hud == null or _movement == null or not is_instance_valid(_movement):
		return

	var body: Node2D = _movement.get_updated_body()
	if body == null:
		return

	if body.position.y > RESPAWN_BELOW:
		body.position = _start_position
		_movement.set_velocity(Vector2.ZERO)

	var floor_result: FloorResult = _movement.get_current_floor()
	var immersion: float = _movement.get_immersion_depth_at(Transform2D(0.0, body.position))

	_hud.text = "\n".join([
		"mode      %s" % _movement.get_movement_mode(),
		"position  (%.0f, %.0f)" % [body.position.x, body.position.y],
		"velocity  (%.0f, %.0f)" % [_movement.get_velocity().x, _movement.get_velocity().y],
		"ground    %s   crouched %s" % [_movement.is_on_ground(), _movement.is_crouching()],
		"floor     walkable=%s dist=%.2f" % [floor_result.is_walkable_floor(), floor_result.get_distance_to_floor()],
		"immersion %.2f   base %s" % [immersion, _movement.get_state().has_base()],
		"",
		"A/D move   W/S up-down (S crouches on the ground)   Space jump   E dash",
	])


# --- Building ----------------------------------------------------------------

func _add_block(x: float, y: float, w: float, h: float, colour: Color) -> void:
	var body := StaticBody2D.new()
	body.position = Vector2(x + w * 0.5, y + h * 0.5)

	var shape := CollisionShape2D.new()
	var rect := RectangleShape2D.new()
	rect.size = Vector2(w, h)
	shape.shape = rect
	body.add_child(shape)

	var visual := Polygon2D.new()
	visual.polygon = PackedVector2Array([
		Vector2(-w * 0.5, -h * 0.5), Vector2(w * 0.5, -h * 0.5),
		Vector2(w * 0.5, h * 0.5), Vector2(-w * 0.5, h * 0.5),
	])
	visual.color = colour
	body.add_child(visual)

	add_child(body)


func _add_ramp(x: float, y: float, points: Array, colour: Color) -> void:
	var body := StaticBody2D.new()
	body.position = Vector2(x, y)

	var packed := PackedVector2Array()
	for point in points:
		packed.append(point)

	var shape := CollisionShape2D.new()
	var convex := ConvexPolygonShape2D.new()
	convex.points = packed
	shape.shape = convex
	body.add_child(shape)

	var visual := Polygon2D.new()
	visual.polygon = packed
	visual.color = colour
	body.add_child(visual)

	add_child(body)


func _add_platform(node_name: String, x: float, y: float, w: float, h: float) -> AnimatableBody2D:
	var body := AnimatableBody2D.new()
	body.name = node_name

	# Without this the physics server never learns the platform's velocity, and
	# stepping off one would not carry its momentum with you.
	body.sync_to_physics = true
	body.position = Vector2(x, y)

	var shape := CollisionShape2D.new()
	var rect := RectangleShape2D.new()
	rect.size = Vector2(w, h)
	shape.shape = rect
	body.add_child(shape)

	var visual := Polygon2D.new()
	visual.polygon = PackedVector2Array([
		Vector2(-w * 0.5, -h * 0.5), Vector2(w * 0.5, -h * 0.5),
		Vector2(w * 0.5, h * 0.5), Vector2(-w * 0.5, h * 0.5),
	])
	visual.color = PLATFORM
	body.add_child(visual)

	add_child(body)
	return body


func _add_water(x: float, y: float, w: float, h: float) -> void:
	var volume := WaterVolume2D.new()
	volume.position = Vector2(x + w * 0.5, y + h * 0.5)
	volume.water_velocity = Vector2(60.0, 0.0)

	var shape := CollisionShape2D.new()
	var rect := RectangleShape2D.new()
	rect.size = Vector2(w, h)
	shape.shape = rect
	volume.add_child(shape)

	var visual := Polygon2D.new()
	visual.polygon = PackedVector2Array([
		Vector2(-w * 0.5, -h * 0.5), Vector2(w * 0.5, -h * 0.5),
		Vector2(w * 0.5, h * 0.5), Vector2(-w * 0.5, h * 0.5),
	])
	visual.color = WATER
	volume.add_child(visual)

	add_child(volume)


func _add_updraft(zone: Rect2) -> void:
	# Only a marker. The switch to Flying is the transition's doing, and it reads
	# the rectangle rather than this node - a transition that queried the scene
	# could not be replayed.
	var visual := Polygon2D.new()
	visual.position = zone.position
	visual.polygon = PackedVector2Array([
		Vector2(0, 0), Vector2(zone.size.x, 0), zone.size, Vector2(0, zone.size.y),
	])
	visual.color = UPDRAFT
	add_child(visual)


func _add_sign(x: float, y: float, text: String) -> void:
	var label := Label.new()
	label.position = Vector2(x, y)
	label.text = text
	label.add_theme_color_override("font_color", Color(0.85, 0.86, 0.88))
	add_child(label)


# --- The updraft -------------------------------------------------------------

class FlyZoneTransition extends MovementModeTransition:
	## Inside the rectangle the character flies; outside it, it falls again.
	##
	## Geometry, not a node query: everything this reads comes from the state
	## being simulated, which is what lets a replayed tick reach the same answer.
	var zone: Rect2

	func _evaluate(params: MovementTickParams) -> StringName:
		var state: MovementState = params.get_out_state()
		var inside: bool = zone.has_point(Vector2(state.position.x, state.position.y))
		var flying: bool = state.movement_mode == &"Flying"

		if inside and not flying:
			return &"Flying"

		if flying and not inside:
			return &"Falling"

		return &""
