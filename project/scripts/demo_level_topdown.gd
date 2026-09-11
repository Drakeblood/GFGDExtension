extends Level
## The top-down playground: the same CharacterMovementComponent2D as the side-on
## map, in a view with no gravity axis on it.
##
## Everything here is [code]FlyingMode2D[/code] - no gravity, no floor test, input
## not flattened, plain collide-and-slide - which is what a top-down mover is. The
## pawn scene sets gravity to 0, starts in Flying and turns crouching off; see the
## "Top-down 2D" section of the movement reference for why each of those matters.
##
## Geometry is a table, the same as the side-on map. A wall row is
## [x, y, width, height] with (x, y) the top-left corner. The room's interior is
## x 0..2400, y 0..1600.

const WALL := Color(0.20, 0.22, 0.26)
const PILLAR := Color(0.30, 0.33, 0.38)
const MUD := Color(0.34, 0.28, 0.18, 0.55)

## The current in the mud, in pixels per second. With gravity at 0 the swimming
## mode's buoyancy term vanishes and what is left is exactly flying plus this.
const MUD_CURRENT := Vector2(0.0, 220.0)
const MUD_RECT := Rect2(1880.0, 500.0, 420.0, 700.0)

const WALLS: Array = [
	# The room.
	[-40.0, -40.0, 2480.0, 40.0, WALL],
	[-40.0, 1600.0, 2480.0, 40.0, WALL],
	[-40.0, 0.0, 40.0, 1600.0, WALL],
	[2400.0, 0.0, 40.0, 1600.0, WALL],

	# 2 - pillars to weave between.
	[700.0, 360.0, 80.0, 80.0, PILLAR],
	[700.0, 1160.0, 80.0, 80.0, PILLAR],
	[920.0, 760.0, 80.0, 80.0, PILLAR],

	# 3 - a corridor with a 200 px gap, for sliding along a wall into an opening.
	[1180.0, 0.0, 60.0, 700.0, PILLAR],
	[1180.0, 900.0, 60.0, 700.0, PILLAR],
]

## Angled walls, as polygons: [origin_x, origin_y, [points...], colour].
const ANGLED: Array = [
	# 4 - a wedge pointing west, both faces at exactly 45 degrees. Pushing
	# straight east into it slides north above the point and south below it,
	# rather than stopping - slide_move doing its job with no floor anywhere in
	# sight. The flat sides face east, so the vertical edges are round the back.
	[1400.0, 400.0, [Vector2(400, 0), Vector2(0, 400), Vector2(400, 400)], PILLAR],
	[1400.0, 800.0, [Vector2(400, 400), Vector2(0, 0), Vector2(400, 0)], PILLAR],
]

const SIGNS: Array = [
	[80.0, 120.0, "1 - Moving\nWASD in any of eight directions. No gravity, so nothing pulls you\nanywhere. Diagonals are normalised - no speed bonus."],
	[620.0, 120.0, "2 - Walls\nPillars stop you at exactly the capsule radius, and pushing\ndiagonally into one slides you along it."],
	[1010.0, 120.0, "3 - Corridor\nSlide along the wall\nto find the gap."],
	[1330.0, 120.0, "4 - Angled walls\n45 degree faces. Push\nstraight in and you slide."],
	[1880.0, 300.0, "5 - Mud\nA WaterVolume2D. With gravity at 0 the swimming mode has no\nbuoyancy left and is simply flying plus a current - so this is\na ready-made stream, conveyor or mud patch."],
	[80.0, 1440.0, "E dashes. The dash uses OVERRIDE_VELOCITY, not OVERRIDE_ALL_EXCEPT_VERTICAL:\ntop-down the screen's y is a real direction, and the other mix mode would make\na southward dash cover exactly zero pixels."],
]

var _hud: Label
var _movement: CharacterMovementComponent2D


func _ready() -> void:
	print("GFGD demo:      [%s] _ready" % name)

	for row in WALLS:
		_add_wall(row[0], row[1], row[2], row[3], row[4])

	for row in ANGLED:
		_add_angled(row[0], row[1], row[2], row[3])

	for row in SIGNS:
		_add_sign(row[0], row[1], row[2])

	_add_mud()

	_hud = get_node_or_null(^"UI/HUD")


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

	attach_to(pawn.get_pawn_root().get_node_or_null(^"CharacterMovementComponent2D"))


## Caches the movement component for the HUD. Public so a headless test can drive
## the map without a World.
func attach_to(movement: CharacterMovementComponent2D) -> void:
	if movement == null or movement == _movement:
		return

	_movement = movement
	print("GFGD demo: >> %s took over the pawn's CharacterMovementComponent2D" % name)


func _process(_delta: float) -> void:
	if _hud == null or _movement == null or not is_instance_valid(_movement):
		return

	var body: Node2D = _movement.get_updated_body()
	if body == null:
		return

	var velocity: Vector2 = _movement.get_velocity()

	_hud.text = "\n".join([
		"mode      %s" % _movement.get_movement_mode(),
		"position  (%.0f, %.0f)" % [body.position.x, body.position.y],
		"velocity  (%.0f, %.0f)   speed %.0f of %.0f" % [
			velocity.x, velocity.y, velocity.length(), _movement.max_fly_speed],
		"in mud    %s" % (_movement.get_movement_mode() == &"Swimming"),
		"",
		"ground=%s falling=%s  -  both are meaningless here: there is no floor" % [
			_movement.is_on_ground(), _movement.is_falling()],
		"",
		"WASD move   E dash",
	])


# --- Building ----------------------------------------------------------------

func _rect_polygon(w: float, h: float) -> PackedVector2Array:
	return PackedVector2Array([
		Vector2(-w * 0.5, -h * 0.5), Vector2(w * 0.5, -h * 0.5),
		Vector2(w * 0.5, h * 0.5), Vector2(-w * 0.5, h * 0.5),
	])


func _add_wall(x: float, y: float, w: float, h: float, colour: Color) -> void:
	var body := StaticBody2D.new()
	body.position = Vector2(x + w * 0.5, y + h * 0.5)

	var shape := CollisionShape2D.new()
	var rect := RectangleShape2D.new()
	rect.size = Vector2(w, h)
	shape.shape = rect
	body.add_child(shape)

	var visual := Polygon2D.new()
	visual.polygon = _rect_polygon(w, h)
	visual.color = colour
	body.add_child(visual)

	add_child(body)


func _add_angled(x: float, y: float, points: Array, colour: Color) -> void:
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


func _add_mud() -> void:
	var volume := WaterVolume2D.new()
	volume.name = "Mud"
	volume.position = MUD_RECT.position + MUD_RECT.size * 0.5
	volume.water_velocity = MUD_CURRENT

	var shape := CollisionShape2D.new()
	var rect := RectangleShape2D.new()
	rect.size = MUD_RECT.size
	shape.shape = rect
	volume.add_child(shape)

	var visual := Polygon2D.new()
	visual.polygon = _rect_polygon(MUD_RECT.size.x, MUD_RECT.size.y)
	visual.color = MUD
	volume.add_child(visual)

	add_child(volume)


func _add_sign(x: float, y: float, text: String) -> void:
	var label := Label.new()
	label.position = Vector2(x, y)
	label.text = text
	label.add_theme_color_override("font_color", Color(0.85, 0.86, 0.88))
	add_child(label)
