extends Pawn
## The networked pawn.
##
## Movement is predicted: the owning client simulates its own character the
## moment a key goes down, sends the server what it pressed on every physics
## tick, and is corrected if the server disagrees. Everybody else sees the
## server's result. The switch is client_prediction on the scene's
## CharacterMovementComponent; turn it off and this same script runs the
## server-only way, with the client waiting a round trip to see itself move.
##
## Nothing here integrates anything. The pawn only says which way the player
## asked to go; CharacterMovementComponent takes that vector on the physics tick
## and turns it into gravity, friction, collision and a floor.

## Custom input bit for the dash. Sent with the move, so the dash starts on the
## same tick on the client that predicts it and on the server that checks it.
const FLAG_DASH := 1 << 0

var _input_component: InputComponent
var _movement: CharacterMovementComponent


func _possessed(_controller: Controller) -> void:
	# Possession happens wherever the pawn is simulated - on the server, and on
	# the owning client - which is exactly where the tick runs and the signal
	# fires: on the predicting client, on the server, and on every replay.
	_movement = get_pawn_root().get_node_or_null(^"CharacterMovementComponent")
	if _movement != null and not _movement.custom_input_flags.is_connected(_on_custom_input_flags):
		_movement.custom_input_flags.connect(_on_custom_input_flags)


func _setup_input_component(input_component: InputComponent) -> void:
	_input_component = input_component

	input_component.bind_action(&"jump", InputComponent.STARTED, _on_jump)
	input_component.bind_action(&"dash", InputComponent.STARTED, _on_dash)

	# Straight to the ability system, on whichever machine the binding fires. On
	# the owning client its component asks the server - and, for the predicted
	# sprint, runs it at once; on the server, a remote player's presses are
	# ignored here, because the client's request is what counts.
	var asc: AbilitySystemComponent = get_pawn_root().get_node_or_null(^"AbilitySystemComponent")
	if asc != null:
		input_component.bind_action(&"activate_test", InputComponent.STARTED,
			func() -> void: asc.ability_local_input_pressed(&"activate_test"))
		input_component.bind_action(&"activate_test", InputComponent.COMPLETED,
			func() -> void: asc.ability_local_input_released(&"activate_test"))


func _unpossessed() -> void:
	_input_component = null


func _gather_movement_input(_delta: float) -> void:
	# Called on the physics tick, right before the movement component takes the
	# vector out - exactly once per simulated step, so a half-pushed stick keeps
	# its magnitude. Reading this in _process would accumulate once per rendered
	# frame instead, and the component would have to clamp it.
	#
	# wants_movement_input(), not has_authority() and not is_locally_controlled():
	# it is true wherever what is read here actually drives the character - the
	# owning client when it predicts, the server when nobody does.
	if _input_component == null or not wants_movement_input():
		return

	var move: Vector2 = _input_component.get_vector(&"move_left", &"move_right", &"move_forward", &"move_back")
	add_movement_input(Vector3(move.x, 0.0, move.y))


func _on_jump() -> void:
	if _movement != null and wants_movement_input():
		_movement.jump()


func _on_dash() -> void:
	# The button only sets a bit. What the bit does happens in
	# _on_custom_input_flags, inside the tick it was sent with.
	if _movement != null and wants_movement_input():
		_movement.add_custom_input_flags(FLAG_DASH)


func _on_custom_input_flags(flags: int) -> void:
	if flags & FLAG_DASH == 0:
		return

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
	dash.mix_mode = ProposedMove.OVERRIDE_ALL_EXCEPT_VERTICAL
	dash.finish_velocity_mode = LayeredMove.CLAMP_VELOCITY
	dash.finish_clamp_speed = _movement.max_walk_speed
	_movement.queue_layered_move(dash)
