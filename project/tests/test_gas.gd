extends SceneTree

# The ability system on one machine: every rule an effect, a modifier or an
# ability follows, checked by number rather than by eye.
#
# The attribute formula, stacking (aggregate, limits, MULTIPLY raised to the
# stack, removing single stacks, a duration that drops one stack at a time),
# the three magnitude types (a level curve, a value set by the caller, a
# snapshot of the source's attribute), effects applied to another component,
# periodic effects that tick on application, gameplay cues, and an ability's
# cost and cooldown - commit, refuse, report why, and recover once the
# cooldown runs out.
#
# Replication is test_gas_replication.gd's job; everything here runs with no
# network, where a component is its own authority.
#
# Run with:
#   godot --headless --path project --script res://tests/test_gas.gd

const TOLERANCE := 0.0001

var failures: Array[String] = []
var target: AbilitySystemComponent
var source: AbilitySystemComponent
var cues: Array[Array] = []
var failures_seen: Array[StringName] = []


# Commits cost and cooldown, then ends itself - the shape of most abilities.
class CommitAbility extends GameplayAbility:
	func _activate_ability() -> void:
		end_ability(not commit_ability())


func _check(name: String, complaint: String) -> void:
	print("    %s %s%s" % ["PASS" if complaint.is_empty() else "FAIL", name,
		"" if complaint.is_empty() else "  -  " + complaint])
	if not complaint.is_empty():
		failures.append("%s: %s" % [name, complaint])


func _near(name: String, actual: float, expected: float) -> void:
	_check(name, "" if absf(actual - expected) < TOLERANCE else "got %.4f, expected %.4f" % [actual, expected])


func _make_component(attributes: Dictionary) -> AbilitySystemComponent:
	var set := AttributeSet.new()
	for attribute_name in attributes:
		set.define_attribute(attribute_name, attributes[attribute_name])

	var holder := Node.new()
	root.add_child(holder)
	var component := AbilitySystemComponent.new()
	component.attribute_set = set
	holder.add_child(component)
	return component


func _modifier(attribute: StringName, operation: int, magnitude: float) -> AttributeModifier:
	var modifier := AttributeModifier.new()
	modifier.attribute = attribute
	modifier.operation = operation
	modifier.magnitude = magnitude
	return modifier


func _effect(policy: int, modifiers: Array, duration := 0.0) -> GameplayEffect:
	var effect := GameplayEffect.new()
	effect.duration_policy = policy
	effect.duration = duration
	var typed: Array[AttributeModifier] = []
	for modifier in modifiers:
		typed.append(modifier)
	effect.modifiers = typed
	return effect


func _tags(names: PackedStringArray) -> GameplayTagContainer:
	var container := GameplayTagContainer.new()
	container.set_tags(names)
	return container


func _initialize() -> void:
	target = _make_component({ &"health": 100.0, &"max_health": 100.0, &"stamina": 50.0, &"strength": 10.0 })
	source = _make_component({ &"strength": 10.0 })
	target.gameplay_cue.connect(func(tag: StringName, event: int, _parameters: Dictionary) -> void: cues.append([tag, event]))
	target.ability_activation_failed.connect(func(_ability: GameplayAbility, reason: StringName) -> void: failures_seen.append(reason))
	_run.call_deferred()


func _wait(seconds: float) -> void:
	var frames := int(ceil(seconds * Engine.physics_ticks_per_second)) + 1
	for i in frames:
		await physics_frame


func _run() -> void:
	# One frame, so both components have run _ready.
	await physics_frame

	print("=== formula ===")
	var add := _effect(GameplayEffect.INFINITE, [_modifier(&"max_health", AttributeModifier.ADD, 20.0)])
	var mul := _effect(GameplayEffect.INFINITE, [_modifier(&"max_health", AttributeModifier.MULTIPLY, 1.5)])
	var add_id := target.apply_gameplay_effect_to_self(add)
	var mul_id := target.apply_gameplay_effect_to_self(mul)
	_near("(base + ADD) * MULTIPLY", target.get_attribute_value(&"max_health"), 180.0)
	_near("the base is left alone", target.get_attribute_base_value(&"max_health"), 100.0)
	target.remove_active_gameplay_effect(add_id)
	target.remove_active_gameplay_effect(mul_id)
	_near("and removing both puts it back", target.get_attribute_value(&"max_health"), 100.0)

	print("=== stacking ===")
	var marker := _effect(GameplayEffect.INFINITE, [_modifier(&"strength", AttributeModifier.ADD, 2.0)])
	marker.stacking_type = GameplayEffect.STACKING_AGGREGATE
	marker.stack_limit = 3
	var first_id := target.apply_gameplay_effect_to_self(marker)
	var same := true
	for i in 3:
		same = same and target.apply_gameplay_effect_to_self(marker) == first_id
	_check("an aggregate effect applied again is the same active effect", "" if same else "a new id was handed out")
	_check("stacks stop at the limit", "" if target.get_active_effect_stack_count(first_id) == 3 else "stack %d" % target.get_active_effect_stack_count(first_id))
	_near("an ADD counts once per stack", target.get_attribute_value(&"strength"), 16.0)
	target.remove_active_gameplay_effect(first_id, 1)
	_near("removing one stack takes one stack's worth", target.get_attribute_value(&"strength"), 14.0)
	target.remove_active_gameplay_effect(first_id)
	_near("removing the effect takes the rest", target.get_attribute_value(&"strength"), 10.0)

	var boost := _effect(GameplayEffect.INFINITE, [_modifier(&"strength", AttributeModifier.MULTIPLY, 1.1)])
	boost.stacking_type = GameplayEffect.STACKING_AGGREGATE
	var boost_id := target.apply_gameplay_effect_to_self(boost)
	target.apply_gameplay_effect_to_self(boost)
	_near("a MULTIPLY is raised to the stack", target.get_attribute_value(&"strength"), 12.1)
	target.remove_active_gameplay_effect(boost_id)

	var dose := _effect(GameplayEffect.HAS_DURATION, [_modifier(&"strength", AttributeModifier.ADD, 1.0)], 0.2)
	dose.stacking_type = GameplayEffect.STACKING_AGGREGATE
	dose.stack_expiration = GameplayEffect.REMOVE_SINGLE_AND_REFRESH
	var dose_id := target.apply_gameplay_effect_to_self(dose)
	target.apply_gameplay_effect_to_self(dose)
	await _wait(0.25)
	_check("REMOVE_SINGLE_AND_REFRESH drops one stack when time runs out",
		"" if target.get_active_effect_stack_count(dose_id) == 1 else "stack %d" % target.get_active_effect_stack_count(dose_id))
	_near("and the attribute follows", target.get_attribute_value(&"strength"), 11.0)
	await _wait(0.25)
	_check("and the last one goes after another duration", "" if target.get_active_effect(dose_id) == null else "still active")

	print("=== magnitudes ===")
	var curve := Curve.new()
	curve.min_domain = 1.0
	curve.max_domain = 5.0
	curve.max_value = 10.0
	curve.add_point(Vector2(1.0, 1.0))
	curve.add_point(Vector2(5.0, 3.0))
	var scaled := _modifier(&"max_health", AttributeModifier.ADD, 10.0)
	scaled.level_curve = curve
	var scaled_effect := _effect(GameplayEffect.INFINITE, [scaled])
	var level_one := target.apply_gameplay_effect_to_self(scaled_effect, 1.0)
	_near("a level curve at level 1", target.get_attribute_value(&"max_health"), 110.0)
	target.remove_active_gameplay_effect(level_one)
	var level_five := target.apply_gameplay_effect_to_self(scaled_effect, 5.0)
	_near("and at level 5", target.get_attribute_value(&"max_health"), 130.0)
	target.remove_active_gameplay_effect(level_five)

	var hit := _modifier(&"health", AttributeModifier.ADD, 0.0)
	hit.magnitude_type = AttributeModifier.SET_BY_CALLER
	hit.set_by_caller_name = &"Damage"
	var hit_effect := _effect(GameplayEffect.INSTANT, [hit])
	var spec := target.make_outgoing_spec(hit_effect)
	spec.set_set_by_caller_magnitude(&"Damage", -25.0)
	target.apply_gameplay_effect_spec_to_self(spec)
	_near("a set-by-caller value is what the spec was given", target.get_attribute_value(&"health"), 75.0)

	var punch := _modifier(&"health", AttributeModifier.ADD, 0.0)
	punch.magnitude_type = AttributeModifier.ATTRIBUTE_BASED
	punch.backing_attribute = &"strength"
	punch.attribute_source = AttributeModifier.SOURCE
	punch.coefficient = -2.0
	var punch_effect := _effect(GameplayEffect.INSTANT, [punch])
	var result := source.apply_gameplay_effect_to_target(punch_effect, target)
	_check("an effect applied to a target lands on the target", "" if result == 0 else "returned %d" % result)
	_near("an attribute-based magnitude reads the source", target.get_attribute_value(&"health"), 55.0)
	_near("and the source is untouched", source.get_attribute_value(&"health"), 0.0)

	print("=== periodic ===")
	var bleed := _effect(GameplayEffect.INFINITE, [_modifier(&"health", AttributeModifier.ADD, -1.0)])
	bleed.period = 0.1
	bleed.execute_period_on_application = true
	var bleed_id := target.apply_gameplay_effect_to_self(bleed)
	_near("execute_period_on_application ticks at once", target.get_attribute_value(&"health"), 54.0)
	await _wait(0.25)
	var after_ticks: float = target.get_attribute_value(&"health")
	target.remove_active_gameplay_effect(bleed_id)
	_check("and then once per period", "" if after_ticks <= 52.0 and after_ticks >= 51.0 else "health %.1f" % after_ticks)

	print("=== cues ===")
	cues.clear()
	var flash := _effect(GameplayEffect.INSTANT, [])
	flash.gameplay_cue_tags = _tags(["Cue.Test"])
	target.apply_gameplay_effect_to_self(flash)
	var burn := _effect(GameplayEffect.HAS_DURATION, [], 0.1)
	burn.gameplay_cue_tags = _tags(["Cue.Burning"])
	target.apply_gameplay_effect_to_self(burn)
	_check("an instant effect executes its cue",
		"" if cues.has([&"Cue.Test", AbilitySystemComponent.CUE_EXECUTED]) else str(cues))
	_check("a duration effect adds its cue", "" if cues.has([&"Cue.Burning", AbilitySystemComponent.CUE_ADDED]) else str(cues))
	_check("which is active while it lasts", "" if target.is_gameplay_cue_active(&"Cue.Burning") else "not active")
	await _wait(0.15)
	_check("and removed when it ends", "" if cues.has([&"Cue.Burning", AbilitySystemComponent.CUE_REMOVED]) else str(cues))

	print("=== cost and cooldown ===")
	var cost := _effect(GameplayEffect.INSTANT, [_modifier(&"stamina", AttributeModifier.ADD, -20.0)])
	var cooldown := _effect(GameplayEffect.HAS_DURATION, [], 0.3)
	cooldown.granted_tags = _tags(["Cooldown.Test"])
	var ability := CommitAbility.new()
	ability.ability_name = &"Commit"
	ability.cost_effect = cost
	ability.cooldown_effect = cooldown
	var handle := target.give_ability(ability)
	_check("give_ability returns a handle", "" if handle > 0 else "handle %d" % handle)

	failures_seen.clear()
	_check("the first activation goes through", "" if target.try_activate_ability(&"Commit") else "refused")
	_near("and pays its cost", target.get_attribute_value(&"stamina"), 30.0)
	var granted: GameplayAbility = target.find_ability_by_handle(handle)
	_check("and starts its cooldown", "" if granted.get_cooldown_time_remaining() > 0.25 else "%.3f s left" % granted.get_cooldown_time_remaining())
	_check("a second one inside the cooldown is refused", "" if not target.try_activate_ability(&"Commit") else "went through")
	_check("and says why", "" if failures_seen == [&"cooldown"] else str(failures_seen))

	await _wait(0.35)
	_check("after the cooldown it goes through again", "" if target.try_activate_ability(&"Commit") else "refused: %s" % granted.get_last_failure_reason())
	_near("paying again", target.get_attribute_value(&"stamina"), 10.0)
	await _wait(0.35)
	failures_seen.clear()
	_check("with 10 stamina left a 20 cost is refused", "" if not target.try_activate_ability(&"Commit") else "went through")
	_check("and says why", "" if failures_seen == [&"cost"] else str(failures_seen))

	print("=== offline ===")
	_check("with no network a component is its own authority",
		"" if target.has_authority() and not target.is_replicated_client() else "thinks it is a client")

	if failures.is_empty():
		print("test_gas: all checks passed")
		quit(0)
	else:
		print("test_gas: %d check(s) failed" % failures.size())
		quit(1)
