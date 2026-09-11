extends Pawn
## The top-down playground's pawn.
##
## The same component as the side-on one, configured for a view with no gravity
## axis on it: gravity 0, starting in Flying, crouching off. See the "Top-down 2D"
## section of the movement reference for why each of those is needed.
##
## WASD move in all eight directions. E dashes. There is no jump: up is not a
## direction you can leave the ground in here, and jump() would only shove the
## character 420 px/s north for no reason.

## Deliberately OVERRIDE_VELOCITY and not OVERRIDE_ALL_EXCEPT_VERTICAL, which is
## what the side-on pawn uses. "Vertical" means the screen's y, and top-down that
## is an ordinary direction of travel - with the other mix mode an eastward dash
## works and a southward one covers exactly zero pixels.
const DASH_MIX_MODE := ProposedMove.OVERRIDE_VELOCITY
const DASH_SPEED := 1400.0
const DASH_SECONDS := 0.18

var _input_component: InputComponent
var _movement: CharacterMovementComponent2D
var _visual: Polygon2D
var _facing := Vector2.RIGHT


func _ready() -> void:
	_visual = get_pawn_root().get_node_or_null(^"Visual")
	set_process(true)


func _process(_delta: float) -> void:
	# Cosmetic only. A 2D MovementState carries no rotation - which way a
	# character points is a sprite's business and the solver never hears about it.
	if _visual != null:
		_visual.rotation = _facing.angle()


func _setup_input_component(input_component: InputComponent) -> void:
	_input_component = input_component
	_movement = get_pawn_root().get_node_or_null(^"CharacterMovementComponent2D")

	input_component.bind_action(&"activate_test", InputComponent.STARTED, _on_dash)


func _unpossessed() -> void:
	_input_component = null


func _gather_movement_input(_delta: float) -> void:
	if _input_component == null or _movement == null or not has_authority():
		return

	# Nothing is flattened and nothing is dropped: both axes are movement here.
	var move: Vector2 = _input_component.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
	add_movement_input(Vector3(move.x, move.y, 0.0))

	if not move.is_zero_approx():
		_facing = move.normalized()


func _on_dash() -> void:
	if _movement == null or not has_authority():
		return

	# Where the character is going, or where it last went if it is standing still.
	var direction: Vector2 = _movement.get_velocity()
	direction = direction.normalized() if not direction.is_zero_approx() else _facing

	var dash := LinearVelocityLayeredMove.new()
	dash.velocity = Vector3(direction.x * DASH_SPEED, direction.y * DASH_SPEED, 0.0)
	dash.duration = DASH_SECONDS
	dash.mix_mode = DASH_MIX_MODE
	dash.finish_velocity_mode = LayeredMove.CLAMP_VELOCITY
	dash.finish_clamp_speed = _movement.max_fly_speed
	_movement.queue_layered_move(dash)
