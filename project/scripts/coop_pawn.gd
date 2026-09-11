extends Pawn
## Demo pawn for local co-op.
##
## The point of this script is the movement read: it goes through the
## InputComponent, which resolves to the owning player's PlayerInput, so each
## pawn only ever sees its own pad. Reading Input directly here would move both
## pawns at once - that is exactly the problem PlayerInput exists to solve.
##
## What happens to that vector afterwards is CharacterMovementComponent's
## business; this script never touches a position.

var _input_component: InputComponent
var _movement: CharacterMovementComponent


func _setup_input_component(input_component: InputComponent) -> void:
	_input_component = input_component
	_movement = get_pawn_root().get_node_or_null(^"CharacterMovementComponent")
	print("GFGD demo: >> %s._setup_input_component" % get_pawn_root().name)

	input_component.bind_action(&"jump", InputComponent.STARTED, _on_jump)
	input_component.bind_action(&"activate_test", InputComponent.STARTED,
		func() -> void: print("GFGD demo:    %s fired activate_test" % get_pawn_root().name))


func _unpossessed() -> void:
	_input_component = null


func _gather_movement_input(_delta: float) -> void:
	# On the physics tick, so the input is gathered exactly once per simulated
	# step and an analog stick keeps its magnitude.
	if _input_component == null or not has_authority():
		return

	var move: Vector2 = _input_component.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
	add_movement_input(Vector3(move.x, 0.0, move.y))


func _on_jump() -> void:
	if _movement != null and has_authority():
		_movement.jump()
