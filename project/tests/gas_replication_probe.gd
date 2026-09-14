extends Node
# The hands and eyes of test_gas_replication.gd, loaded into every process of it
# through the demo's --probe flag, at /root/Probe on each so they can call one
# another.
#
# The server half does what a game server does to a character: buffs it,
# damages it, grants it an ability its client will not be allowed to use, and
# takes the buff away again - each on request, so the client can check each
# result in turn.
#
# The owning client drives its own online pawn and checks what replication
# promises it: the server's attributes, tags, effects and cues arrive; its
# abilities arrive, with their scripts; a LOCAL_PREDICTED ability runs at once
# and is paid for by the server; a SERVER_ONLY one runs on the server and is
# heard here as a cue; a predicted activation the server refuses is taken back.
#
# The observer (--observer) is a second client that checks the other half of
# the contract: it sees the first player's attributes, tags and effects, and
# none of that player's abilities.

const CLIENT_DEADLINE := 40.0
const SERVER_DEADLINE := 60.0

var world: World
var failures: Array[String] = []
var elapsed := 0.0
var observer := false
var done := false
var started := false

# Client
var asc: AbilitySystemComponent
var cues: Array = []
var activation_failures: Array = []
var activated: Array = []


func _ready() -> void:
	world = get_tree() as World
	observer = OS.get_cmdline_user_args().has("--observer")

	var from_anyone := {
		"rpc_mode": MultiplayerAPI.RPC_MODE_ANY_PEER,
		"transfer_mode": MultiplayerPeer.TRANSFER_MODE_RELIABLE,
		"call_local": false,
		"channel": 0,
	}
	for method in [&"server_setup", &"server_grant_refused", &"server_grant_tally", &"server_grant_aim", &"server_grant_event_heal", &"server_grant_cue", &"server_remove_buff"]:
		rpc_config(method, from_anyone)



func _process(delta: float) -> void:
	elapsed += delta
	if world == null:
		return

	# Added at startup, while the demo is still a standalone main menu; it is
	# only a client once it has joined.
	if not started and world.is_networked() and not world.has_authority():
		started = true
		_run_client()

	if world.has_authority():
		if elapsed > SERVER_DEADLINE:
			world.quit(0)
		return

	if elapsed > CLIENT_DEADLINE and not done:
		_check("the run finished in time", "deadline reached")
		_finish()


func _check(name: String, complaint: String) -> void:
	print("    %s %s%s" % ["PASS" if complaint.is_empty() else "FAIL", name,
		"" if complaint.is_empty() else "  -  " + complaint])
	if not complaint.is_empty():
		failures.append("%s: %s" % [name, complaint])


func _tag(tag_name: StringName) -> GameplayTag:
	var tag := GameplayTag.new()
	tag.tag_name = tag_name
	return tag


func _tags(names: PackedStringArray) -> GameplayTagContainer:
	var container := GameplayTagContainer.new()
	container.set_tags(names)
	return container


# --- Server ----------------------------------------------------------------------

func _sender_asc() -> AbilitySystemComponent:
	var sender: int = multiplayer.get_remote_sender_id()
	for pawn_root in world.get_pawn_container().get_children():
		var pawn: Pawn = pawn_root.get_node_or_null(^"Pawn")
		if pawn != null and pawn.get_owner_peer_id() == sender:
			return pawn_root.get_node_or_null(^"AbilitySystemComponent")
	return null


var _buff_ids := {}


func server_setup() -> void:
	var target := _sender_asc()
	if target == null:
		return

	# Built in code on purpose: an effect with no resource path is the case the
	# client has to rebuild from what it is sent.
	var buff := GameplayEffect.new()
	buff.duration_policy = GameplayEffect.INFINITE
	var bonus := AttributeModifier.new()
	bonus.attribute = &"max_health"
	bonus.magnitude = 20.0
	buff.modifiers = [bonus] as Array[AttributeModifier]
	buff.effect_tags = _tags(["Effect.Buff"])
	buff.granted_tags = _tags(["Effect.Buff"])
	buff.gameplay_cue_tags = _tags(["Cue.Burning"])
	_buff_ids[target] = target.apply_gameplay_effect_to_self(buff)

	var damage := GameplayEffect.new()
	var hit := AttributeModifier.new()
	hit.attribute = &"health"
	hit.magnitude_type = AttributeModifier.SET_BY_CALLER
	hit.set_by_caller_name = &"Damage"
	damage.modifiers = [hit] as Array[AttributeModifier]
	damage.gameplay_cue_tags = _tags(["Cue.Hit"])
	var spec := target.make_outgoing_spec(damage)
	spec.set_set_by_caller_magnitude(&"Damage", -30.0)
	target.apply_gameplay_effect_spec_to_self(spec)
	print("gas probe (server): buffed and hit %s" % target.get_parent().name)


func server_grant_refused() -> void:
	var target := _sender_asc()
	if target == null:
		return

	# Needs a tag nobody has. The client's copy is a stand-in built from the
	# name alone - this template has no file - so it does not know that, predicts
	# the activation, and has to be told no.
	var refused := GameplayAbility.new()
	refused.ability_name = &"Refused"
	refused.net_execution_policy = GameplayAbility.LOCAL_PREDICTED
	refused.activation_required_tags = _tags(["Status.Test"])
	target.give_ability(refused)


func server_grant_tally() -> void:
	var target := _sender_asc()
	if target == null:
		return

	# Bound to the same button as the sprint. Its template is built in code, so
	# the client gets a stand-in with no script - it predicts nothing but still
	# sends the request, and the server runs the real thing.
	var tally := GameplayAbility.new()
	tally.set_script(load("res://tests/gas_tally_ability.gd"))
	tally.ability_name = &"Tally"
	tally.input_action_name = &"activate_test"
	tally.net_execution_policy = GameplayAbility.LOCAL_PREDICTED
	target.give_ability(tally)


func server_grant_aim() -> void:
	var target := _sender_asc()
	if target != null:
		# From a file, so the client builds the same ability, script and all.
		target.give_ability(load("res://tests/gas_aim_ability.tres"))


func server_grant_event_heal() -> void:
	var target := _sender_asc()
	if target != null:
		target.give_ability(load("res://tests/gas_event_ability.tres"))


func server_grant_cue() -> void:
	var target := _sender_asc()
	if target != null:
		target.give_ability(load("res://tests/gas_cue_ability.tres"))


func server_remove_buff() -> void:
	var target := _sender_asc()
	if target != null and _buff_ids.has(target):
		target.remove_active_gameplay_effect(_buff_ids[target])


# --- Client ----------------------------------------------------------------------

func _frames(count: int) -> void:
	for i in count:
		await get_tree().physics_frame


# Waits up to seconds for condition to hold; returns whether it did.
func _until(condition: Callable, seconds := 3.0) -> bool:
	var limit := int(seconds * Engine.physics_ticks_per_second)
	for i in limit:
		if condition.call():
			return true
		await get_tree().physics_frame
	return condition.call()


func _own_asc() -> AbilitySystemComponent:
	for pawn_root in world.get_pawn_container().get_children():
		var pawn: Pawn = pawn_root.get_node_or_null(^"Pawn")
		if pawn != null and pawn.is_locally_controlled():
			return pawn_root.get_node_or_null(^"AbilitySystemComponent")
	return null


func _other_asc() -> AbilitySystemComponent:
	for pawn_root in world.get_pawn_container().get_children():
		var pawn: Pawn = pawn_root.get_node_or_null(^"Pawn")
		if pawn != null and not pawn.is_locally_controlled():
			return pawn_root.get_node_or_null(^"AbilitySystemComponent")
	return null


func _run_client() -> void:
	if observer:
		await _run_observer()
	else:
		await _run_owner()
	_finish()


func _run_owner() -> void:
	var found := await _until(func() -> bool:
		var candidate := _own_asc()
		return candidate != null and candidate.is_replication_state_received(), 15.0)
	if not found:
		_check("the owning client receives its component's state", "nothing arrived")
		return
	asc = _own_asc()
	asc.gameplay_cue.connect(func(tag: StringName, event: int, _p: Dictionary) -> void: cues.append([tag, event]))
	asc.ability_activation_failed.connect(func(ability: GameplayAbility, reason: StringName) -> void:
		activation_failures.append([ability.ability_name, reason]))
	asc.ability_activated.connect(func(ability: GameplayAbility) -> void: activated.append(ability.ability_name))

	print("=== the owner's view ===")
	_check("the component knows it is a replicated client", "" if asc.is_replicated_client() else "it thinks it is the authority")
	var sprint: GameplayAbility = asc.find_ability_by_name(&"Sprint")
	var shout: GameplayAbility = asc.find_ability_by_name(&"Shout")
	_check("the granted abilities arrive, with the server's handles",
		"" if sprint != null and shout != null and sprint.get_handle() > 0 and shout.get_handle() > 0 else "have %s" % str(asc.get_activatable_abilities()))
	_check("built from their templates, scripts and all",
		"" if sprint != null and sprint.get_script() != null and sprint.get_cost_effect() != null else "a stand-in arrived")

	rpc_id(1, &"server_setup")
	var buffed := await _until(func() -> bool: return absf(asc.get_attribute_value(&"health") - 70.0) < 0.01 and absf(asc.get_attribute_value(&"max_health") - 120.0) < 0.01)
	_check("the server's attribute changes arrive", "" if buffed else "health %.1f, max_health %.1f" % [asc.get_attribute_value(&"health"), asc.get_attribute_value(&"max_health")])
	_check("so do the tags an effect grants", "" if asc.has_matching_gameplay_tag(_tag(&"Effect.Buff")) else "no Effect.Buff")
	var effects := asc.get_active_effects()
	var mirrored: bool = effects.size() == 1 and (effects[0] as ActiveGameplayEffect).get_effect().effect_tags != null \
		and (effects[0] as ActiveGameplayEffect).get_effect().effect_tags.has_tag(_tag(&"Effect.Buff"))
	_check("and the effect itself, rebuilt though it has no file", "" if mirrored else "effects: %s" % str(effects))
	_check("an instant effect's cue is heard", "" if cues.has([&"Cue.Hit", AbilitySystemComponent.CUE_EXECUTED]) else str(cues))
	_check("a lasting effect's cue is running", "" if asc.is_gameplay_cue_active(&"Cue.Burning") else str(cues))

	print("=== a predicted ability ===")
	var stamina_before: float = asc.get_attribute_value(&"stamina")
	activation_failures.clear()

	# Pressed the way a player presses it: the button's binding runs on this
	# client, and the button's action state still goes to the server, where the
	# same binding fires again - and must be ignored there, or the server would
	# run the ability twice and refuse this client's request.
	var input: PlayerInput = world.get_local_player(0).get_player_input()

	# Read inside the activation itself: on a loopback the server's answer can
	# be back before the next physics frame, so "is it still predicting" asked a
	# frame later says nothing about whether it ever was.
	var at_activation := {}
	var capture := func(ability: GameplayAbility) -> void:
		# Filled in place: a lambda captures the variable, not the binding, so
		# assigning a new Dictionary here would never be seen outside.
		if ability == sprint and at_activation.is_empty():
			at_activation["predicting"] = ability.is_predicting()
			at_activation["sprinting"] = asc.has_matching_gameplay_tag(_tag(&"State.Sprinting"))
			at_activation["stamina"] = asc.get_attribute_value(&"stamina")
	asc.ability_activated.connect(capture)
	input.action_press(&"activate_test", 1.0)
	var started := await _until(func() -> bool: return not at_activation.is_empty(), 0.5)
	input.action_release(&"activate_test")
	asc.ability_activated.disconnect(capture)

	_check("a LOCAL_PREDICTED ability starts the moment its button goes down, as a prediction",
		"" if started and at_activation["predicting"] else "activation seen: %s" % str(at_activation))
	_check("its tags are owned at once", "" if started and at_activation["sprinting"] else str(at_activation))
	_check("but nothing is paid until the server says so",
		"" if started and absf(at_activation["stamina"] - stamina_before) < 0.01 else str(at_activation))
	var paid := await _until(func() -> bool: return absf(asc.get_attribute_value(&"stamina") - (stamina_before - 20.0)) < 0.01)
	_check("the server runs it too, and its cost arrives", "" if paid else "stamina %.1f" % asc.get_attribute_value(&"stamina"))
	var cooling := await _until(func() -> bool: return asc.has_matching_gameplay_tag(_tag(&"Cooldown.Sprint")))
	_check("so does its cooldown", "" if cooling else "no Cooldown.Sprint")
	_check("the server confirmed the prediction", "" if not sprint.is_predicting() and activation_failures.is_empty() else "predicting=%s failures=%s" % [sprint.is_predicting(), str(activation_failures)])
	await _frames(10)
	_check("and paid once, not once per path the press took",
		"" if absf(asc.get_attribute_value(&"stamina") - (stamina_before - 20.0)) < 0.01 else "stamina %.1f" % asc.get_attribute_value(&"stamina"))
	var ended := await _until(func() -> bool: return not sprint.get_is_active() and not asc.has_matching_gameplay_tag(_tag(&"State.Sprinting")))
	_check("when it ends, its tags go on both sides", "" if ended else "State.Sprinting count %d" % asc.get_gameplay_tag_count(_tag(&"State.Sprinting")))
	# Ended after 0.3 s, and the cooldown lasts a whole second.
	activation_failures.clear()
	_check("a second press inside the cooldown is refused here, without asking",
		"" if not asc.try_activate_ability(&"Sprint") and activation_failures == [[&"Sprint", &"cooldown"]] else str(activation_failures))

	print("=== a server-only ability ===")
	cues.clear()
	activated.clear()
	_check("a SERVER_ONLY ability is only asked for", "" if asc.try_activate_ability(&"Shout") and not shout.get_is_active() else "it ran here")
	var shouted := await _until(func() -> bool: return cues.has([&"Cue.Shout", AbilitySystemComponent.CUE_EXECUTED]))
	_check("the server runs it, and its cue is heard here", "" if shouted else str(cues))
	_check("and this client is told it ran", "" if activated.has(&"Shout") else str(activated))

	print("=== a refused prediction ===")
	rpc_id(1, &"server_grant_refused")
	var granted := await _until(func() -> bool: return asc.find_ability_by_name(&"Refused") != null)
	_check("an ability granted later arrives", "" if granted else "never arrived")
	if granted:
		var refused: GameplayAbility = asc.find_ability_by_name(&"Refused")
		activation_failures.clear()
		_check("this client predicts it", "" if asc.try_activate_ability(&"Refused") and refused.get_is_active() else "not active")
		var taken_back := await _until(func() -> bool: return activation_failures.has([&"Refused", &"rejected"]))
		_check("the server refuses, and the prediction is taken back",
			"" if taken_back and not refused.get_is_active() else "failures %s, active %s" % [str(activation_failures), refused.get_is_active()])

	print("=== one press, one activation ===")
	rpc_id(1, &"server_grant_tally")
	var tally_granted := await _until(func() -> bool: return asc.find_ability_by_name(&"Tally") != null)
	_check("an instant ability bound to a button arrives", "" if tally_granted else "never arrived")
	if tally_granted:
		# The sprint on the same button is cooling down by now, so the press
		# reaches the tally alone.
		await _until(func() -> bool: return not asc.has_matching_gameplay_tag(_tag(&"Cooldown.Sprint")), 2.0)
		var health_before: float = asc.get_attribute_value(&"health")
		var press_input: PlayerInput = world.get_local_player(0).get_player_input()
		press_input.action_press(&"activate_test", 1.0)
		await _frames(3)
		press_input.action_release(&"activate_test")
		await _until(func() -> bool: return asc.get_attribute_value(&"health") > health_before + 0.5)
		await _frames(30)
		# The press reaches the server twice - as this client's request and as
		# the action state its controller replicates. Only the request may count.
		_check("the server runs it once for one press, not once per path",
			"" if absf(asc.get_attribute_value(&"health") - (health_before + 1.0)) < 0.01 else "health went from %.1f to %.1f" % [health_before, asc.get_attribute_value(&"health")])

	print("=== an ability task across the network ===")
	rpc_id(1, &"server_grant_aim")
	var aim_granted := await _until(func() -> bool: return asc.find_ability_by_name(&"Aim") != null)
	_check("an ability using sync_target_data arrives", "" if aim_granted else "never arrived")
	if aim_granted:
		# Only this client knows the aim; the server's copy of the component has
		# no such metadata and would heal by 0 on its own.
		asc.set_meta(&"aim", 5)
		var before: float = asc.get_attribute_value(&"health")
		asc.try_activate_ability(&"Aim")
		var healed := await _until(func() -> bool: return asc.get_attribute_value(&"health") > before + 0.5)
		await _frames(10)
		_check("sync_target_data hands the server the client's value",
			"" if healed and absf(asc.get_attribute_value(&"health") - (before + 5.0)) < 0.01 else "health went from %.1f to %.1f" % [before, asc.get_attribute_value(&"health")])

	print("=== an event-triggered prediction ===")
	rpc_id(1, &"server_grant_event_heal")
	var event_granted := await _until(func() -> bool: return asc.find_ability_by_name(&"EventHeal") != null)
	_check("an ability with trigger_event_tags arrives", "" if event_granted else "never arrived")
	if event_granted:
		var before_event: float = asc.get_attribute_value(&"health")
		# Only this client has the event - as if an animation it plays had hit.
		asc.send_gameplay_event(&"Event.Heal", { "amount": 4 })
		await _until(func() -> bool: return asc.get_attribute_value(&"health") > before_event + 0.5)
		await _frames(10)
		_check("the event's payload goes to the server with the predicted activation, and it runs once",
			"" if absf(asc.get_attribute_value(&"health") - (before_event + 4.0)) < 0.01 else "health went from %.1f to %.1f" % [before_event, asc.get_attribute_value(&"health")])

	print("=== a predicted ability's cue ===")
	rpc_id(1, &"server_grant_cue")
	var cue_granted := await _until(func() -> bool: return asc.find_ability_by_name(&"CueOnce") != null)
	_check("an ability that executes a cue arrives", "" if cue_granted else "never arrived")
	if cue_granted:
		cues.clear()
		asc.try_activate_ability(&"CueOnce")
		var heard := cues.count([&"Cue.Test", AbilitySystemComponent.CUE_EXECUTED])
		# Long enough for the server's copy to have run and anything it sent to land.
		await _frames(30)
		var total := cues.count([&"Cue.Test", AbilitySystemComponent.CUE_EXECUTED])
		_check("the predicting client hears it at once", "" if heard == 1 else "%d times at once" % heard)
		_check("and once only - the server does not send it back", "" if total == 1 else "%d times in all" % total)

	print("=== removal ===")
	rpc_id(1, &"server_remove_buff")
	var removed := await _until(func() -> bool: return asc.get_active_effects().is_empty() and not asc.has_matching_gameplay_tag(_tag(&"Effect.Buff")))
	_check("a removed effect goes, with its tags", "" if removed else "effects %d" % asc.get_active_effects().size())
	_check("and its bonus", "" if absf(asc.get_attribute_value(&"max_health") - 100.0) < 0.01 else "max_health %.1f" % asc.get_attribute_value(&"max_health"))
	_check("and its cue", "" if not asc.is_gameplay_cue_active(&"Cue.Burning") and cues.has([&"Cue.Burning", AbilitySystemComponent.CUE_REMOVED]) else str(cues))

	# Long enough for the observer to see the buff before this client leaves.
	await _frames(30)


func _run_observer() -> void:
	# The other player's pawn, buffed by the server at that player's request.
	var seen := await _until(func() -> bool:
		var other := _other_asc()
		return other != null and absf(other.get_attribute_value(&"max_health") - 120.0) < 0.01, 25.0)

	print("=== an observer's view ===")
	_check("another player's attributes arrive", "" if seen else "never saw the buff")
	if not seen:
		return

	var other := _other_asc()
	_check("and their tags", "" if other.has_matching_gameplay_tag(_tag(&"Effect.Buff")) else "no Effect.Buff")
	_check("and, under FULL replication, their effects", "" if other.get_active_effects().size() >= 1 else "no effects")
	_check("but none of their abilities", "" if other.get_activatable_abilities().is_empty() else "%d abilities" % other.get_activatable_abilities().size())


func _finish() -> void:
	if done:
		return
	done = true

	if failures.is_empty():
		print("gas probe (%s): all checks passed" % ("observer" if observer else "owner"))
	else:
		print("gas probe (%s): %d check(s) failed" % ["observer" if observer else "owner", failures.size()])
		for failure in failures:
			print("    - " + failure)

	world.quit(0 if failures.is_empty() else 1)
