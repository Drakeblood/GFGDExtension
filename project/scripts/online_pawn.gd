extends Pawn
## The networked pawn.
##
## Movement runs where the pawn is authoritative - on the server - and the result
## is mirrored to everyone. A client feeds the same action state in by sending
## what its player is holding, so this code is identical in every net mode and
## reads nothing about the network beyond the one authority check.
##
## Nothing here integrates anything. The pawn only says which way the player
## asked to go; CharacterMovementComponent takes that vector on the physics tick
## and turns it into gravity, friction, collision and a floor.

var _input_component: InputComponent
var _movement: CharacterMovementComponent


func _setup_input_component(input_component: InputComponent) -> void:
	_input_component = input_component
	_movement = get_pawn_root().get_node_or_null(^"CharacterMovementComponent")

	input_component.bind_action(&"jump", InputComponent.STARTED, _on_jump)


func _unpossessed() -> void:
	_input_component = null


func _gather_movement_input(_delta: float) -> void:
	# Called on the physics tick, right before the movement component takes the
	# vector out - exactly once per simulated step, so a half-pushed stick keeps
	# its magnitude. Reading this in _process would accumulate once per rendered
	# frame instead, and the component would have to clamp it.
	#
	# has_authority(), not is_locally_controlled(): a remote player's input is read
	# on the server, out of the action state their machine sent, and there
	# is_locally_controlled() is false.
	if _input_component == null or not has_authority():
		return

	var move: Vector2 = _input_component.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
	add_movement_input(Vector3(move.x, 0.0, move.y))


func _on_jump() -> void:
	if _movement != null and has_authority():
		_movement.jump()
