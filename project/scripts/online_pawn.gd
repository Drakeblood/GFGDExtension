extends Pawn
## The networked pawn.
##
## Movement runs where the pawn is authoritative - on the server - and the result
## is mirrored to everyone. A client feeds the same action state in by sending
## what its player is holding, so this code is identical in every net mode and
## reads nothing about the network beyond the one authority check.

const SPEED := 5.0

var _input_component: InputComponent


func _setup_input_component(input_component: InputComponent) -> void:
	_input_component = input_component


func _unpossessed() -> void:
	_input_component = null


func _process(delta: float) -> void:
	if _input_component == null or not has_authority():
		return

	var move: Vector2 = _input_component.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
	if move == Vector2.ZERO:
		return

	add_movement_input(Vector3(move.x, 0.0, move.y))
	get_pawn_root().position += consume_movement_input_vector() * SPEED * delta
