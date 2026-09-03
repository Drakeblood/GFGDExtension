extends Pawn
## Demo pawn for local co-op.
##
## The point of this script is the movement read: it goes through the
## InputComponent, which resolves to the owning player's PlayerInput, so each
## pawn only ever sees its own pad. Reading Input directly here would move both
## pawns at once - that is exactly the problem PlayerInput exists to solve.

const SPEED := 5.0

var _input_component: InputComponent


func _setup_input_component(input_component: InputComponent) -> void:
	_input_component = input_component
	print("GFGD demo: >> %s._setup_input_component" % get_pawn_root().name)

	input_component.bind_action(&"activate_test", InputComponent.STARTED,
		func() -> void: print("GFGD demo:    %s fired activate_test" % get_pawn_root().name))


func _unpossessed() -> void:
	_input_component = null


func _process(delta: float) -> void:
	if _input_component == null:
		return

	var move: Vector2 = _input_component.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
	if move == Vector2.ZERO:
		return

	add_movement_input(Vector3(move.x, 0.0, move.y))

	var pawn_root: Node3D = get_pawn_root()
	pawn_root.global_position += consume_movement_input_vector() * SPEED * delta
