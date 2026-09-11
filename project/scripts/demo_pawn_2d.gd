extends Pawn
## The 2D playground's pawn. Walks, jumps, crouches, swims, flies and dashes -
## one binding per thing CharacterMovementComponent2D does, so the map is a way
## of trying each of them by hand.
##
## A/D move. W and S are up and down: in water and in the air they swim and fly,
## and on the ground S crouches. Space jumps, and jumps out of water. E dashes.

var _input_component: InputComponent
var _movement: CharacterMovementComponent2D
var _visual: Polygon2D
var _standing_half_height := 32.0


func _ready() -> void:
	_visual = get_pawn_root().get_node_or_null(^"Visual")

	# Read from the authored shape rather than from the component: this runs
	# before the component's own _ready, which is where it resolves the capsule
	# and swaps in the private copy it resizes for crouching.
	var shape_node: CollisionShape2D = get_pawn_root().get_node_or_null(^"CollisionShape2D")
	if shape_node != null and shape_node.shape is CapsuleShape2D:
		_standing_half_height = shape_node.shape.height * 0.5

	set_process(true)


func _process(_delta: float) -> void:
	if _visual == null or _movement == null:
		return

	# The capsule shrinks around its own centre when crouching, and the body drops
	# by the same amount so the feet stay put - so scaling the silhouette about
	# its origin tracks it exactly, with no second sum to get wrong.
	_visual.scale.y = _movement.get_capsule_half_height() / _standing_half_height


func _setup_input_component(input_component: InputComponent) -> void:
	_input_component = input_component
	_movement = get_pawn_root().get_node_or_null(^"CharacterMovementComponent2D")

	input_component.bind_action(&"jump", InputComponent.STARTED, _on_jump)
	input_component.bind_action(&"activate_test", InputComponent.STARTED, _on_dash)


func _unpossessed() -> void:
	_input_component = null


func _gather_movement_input(_delta: float) -> void:
	# On the physics tick, so the input is gathered exactly once per simulated
	# step and an analog stick keeps its magnitude.
	if _input_component == null or _movement == null or not has_authority():
		return

	# get_vector's y is already in screen coordinates - W gives -1, S gives +1 -
	# which is exactly what the 2D solver wants, because up there is (0, -1).
	var move: Vector2 = _input_component.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
	add_movement_input(Vector3(move.x, move.y, 0.0))

	# Down means down: in the air or in water it swims and flies, on the ground it
	# crouches. Driven every tick rather than from a pressed/released pair, so the
	# two meanings cannot drift out of step with each other - and so that letting
	# go under a low ceiling is a request to stand rather than an order, which is
	# what the component already treats it as.
	if move.y > 0.5 and _movement.is_on_ground():
		_movement.crouch()
	else:
		_movement.un_crouch()


func _on_jump() -> void:
	if _movement != null and has_authority():
		_movement.jump()


func _on_dash() -> void:
	if _movement == null or not has_authority():
		return

	# Whichever way the character is already going, or right from a standstill.
	var direction: float = signf(_movement.get_velocity().x)
	if is_zero_approx(direction):
		direction = 1.0

	var dash := LinearVelocityLayeredMove.new()
	dash.velocity = Vector3(direction * 1400.0, 0.0, 0.0)
	dash.duration = 0.18
	dash.mix_mode = ProposedMove.OVERRIDE_ALL_EXCEPT_VERTICAL
	dash.finish_velocity_mode = LayeredMove.CLAMP_VELOCITY
	dash.finish_clamp_speed = _movement.max_walk_speed
	_movement.queue_layered_move(dash)
