extends Pawn
## Demo pawn: walks, jumps, crouches and dashes, binds input actions to the
## AbilitySystemComponent when a PlayerController possesses it, and prints its own
## lifecycle so it can be lined up against the level's.
##
## WASD moves, Space jumps, C crouches, F dashes, E runs the ability demo. The
## movement bindings drive CharacterMovementComponent directly; the test level is
## built around trying each of them.

@export var tag: GameplayTag
@export var tags: GameplayTagContainer

## Where the camera sits relative to the character, in world space. The map runs
## along +X, so the camera sits back along -X and looks down the walkway.
##
## This is a fixed-angle follow camera, not a full third-person rig: it never
## turns, because a linear map reads better when the direction of travel stays
## the direction of the screen. It is top_level, so orienting the character to
## its movement does not drag the view round with it.
const CAMERA_OFFSET := Vector3(-9.0, 4.5, 0.0)

## How quickly the camera closes the gap, per second. Exponential, so it is
## frame-rate independent.
const CAMERA_LAG := 6.0

## Roughly chest height on the default capsule - looking at the feet puts the
## horizon in the wrong place.
const CAMERA_AIM := Vector3(0.0, 0.6, 0.0)

var _input_component: InputComponent
var _movement: CharacterMovementComponent
var _camera: Camera3D


func _enter_tree() -> void:
	print("GFGD demo:      [Player/%s] _enter_tree" % name)


func _ready() -> void:
	_camera = get_node_or_null(camera_path)
	if _camera != null:
		# Detached from the pawn's transform: the component turns the body to face
		# its movement, and a camera parented to that would swing with every turn.
		_camera.top_level = true
		_camera.global_position = get_pawn_root().global_position + CAMERA_OFFSET

	set_process(true)


func _process(delta: float) -> void:
	if _camera == null:
		return

	var root: Node3D = get_pawn_root()
	var target: Vector3 = root.global_position + CAMERA_OFFSET
	_camera.global_position = _camera.global_position.lerp(target, 1.0 - exp(-CAMERA_LAG * delta))
	_camera.look_at(root.global_position + CAMERA_AIM)


func _possessed(controller: Controller) -> void:
	print("GFGD demo: >> Player possessed by %s" % controller.get_class())


func _setup_input_component(input_component: InputComponent) -> void:
	print("GFGD demo: >> Player._setup_input_component")
	_input_component = input_component
	_movement = get_pawn_root().get_node_or_null(^"CharacterMovementComponent")

	input_component.bind_action(&"jump", InputComponent.STARTED, _on_jump)
	input_component.bind_action(&"crouch", InputComponent.STARTED, _on_crouch)
	input_component.bind_action(&"crouch", InputComponent.COMPLETED, _on_un_crouch)
	input_component.bind_action(&"dash", InputComponent.STARTED, _on_dash)

	var asc: AbilitySystemComponent = get_pawn_root().get_node("AbilitySystemComponent")
	input_component.bind_action(&"activate_test", InputComponent.STARTED,
		func() -> void: asc.ability_local_input_pressed(&"activate_test"))
	input_component.bind_action(&"activate_test", InputComponent.COMPLETED,
		func() -> void: asc.ability_local_input_released(&"activate_test"))


func _unpossessed() -> void:
	_input_component = null


func _gather_movement_input(_delta: float) -> void:
	# On the physics tick, so the input is gathered exactly once per simulated
	# step and an analog stick keeps its magnitude.
	if _input_component == null or not has_authority():
		return

	var move: Vector2 = _input_component.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
	if move.is_zero_approx():
		return

	# Relative to the camera, not to the world axes. Without this W walks along
	# -Z whatever the view is doing, which on a map laid out along +X means
	# forward is sideways.
	var basis: Basis = _camera.global_basis if _camera != null else Basis()
	var forward: Vector3 = (-basis.z).slide(Vector3.UP)
	var right: Vector3 = basis.x.slide(Vector3.UP)

	if forward.is_zero_approx() or right.is_zero_approx():
		return

	# get_vector's y is -1 for "forward", which is the screen convention.
	add_movement_input((right.normalized() * move.x - forward.normalized() * move.y).limit_length(1.0))


func _on_jump() -> void:
	if _movement != null and has_authority():
		_movement.jump()


func _on_crouch() -> void:
	if _movement != null and has_authority():
		_movement.crouch()


func _on_un_crouch() -> void:
	# A request, not an order: under a low ceiling the component keeps the
	# character down and tries again every tick until there is room.
	if _movement != null and has_authority():
		_movement.un_crouch()


func _on_dash() -> void:
	if _movement == null or not has_authority():
		return

	# Flat, and along whatever direction the character is already travelling -
	# or where it is facing if it is standing still.
	var direction: Vector3 = _movement.get_velocity()
	direction.y = 0.0
	if direction.is_zero_approx():
		direction = -get_pawn_root().global_transform.basis.z
		direction.y = 0.0

	if direction.is_zero_approx():
		return

	var dash := LinearVelocityLayeredMove.new()
	dash.velocity = direction.normalized() * 14.0
	dash.duration = 0.18

	# Vertical is left alone, so a dash off a ledge still falls.
	dash.mix_mode = ProposedMove.OVERRIDE_ALL_EXCEPT_VERTICAL
	dash.finish_velocity_mode = LayeredMove.CLAMP_VELOCITY
	dash.finish_clamp_speed = _movement.max_walk_speed
	_movement.queue_layered_move(dash)
