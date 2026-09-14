extends GameplayAbility
## Q in the 3D playground: a strike that charges while the key is held.
##
## Everything a real ability tends to need, in one place: cost and cooldown
## through commit_ability(), a wait on the player written as a task, a cue that
## runs for as long as the ability does, and damage decided at the moment of
## the hit - a SET_BY_CALLER magnitude on the spec - applied to someone else's
## AbilitySystemComponent. The hit is also sent to the target as Event.Hit, so
## whatever it does about being hit (the dummy's HitReact) is its own ability,
## triggered, and nothing this script has to know about.

const DAMAGE_EFFECT := preload("res://resources/gas/strike_damage.tres")

## Held this long, the strike goes off by itself at full strength.
const MAX_CHARGE := 1.0
const MIN_DAMAGE := 10.0
const MAX_DAMAGE := 40.0

## How far from the character a target can be and still be hit.
const REACH := 3.0

## The nodes a strike can land on. Anything in this group with an
## AbilitySystemComponent child is fair game.
const TARGET_GROUP := &"gas_targets"


func _activate_ability() -> void:
	# The cost (10 mana) and the cooldown (Cooldown.Strike, 0.6 s) are only paid
	# here, so a strike refused for want of mana costs nothing.
	if not commit_ability():
		end_ability(true)
		return

	# Removed by itself when the ability ends, however it ends.
	add_gameplay_cue(&"Cue.Charging")

	# Whichever comes first: letting go, or the charge topping out. If the
	# ability is cancelled while it waits, the line after never runs.
	await wait_any([wait_input_release(), wait_delay(MAX_CHARGE)]).completed

	var charge := clampf(get_time_active() / MAX_CHARGE, 0.0, 1.0)
	var damage := lerpf(MIN_DAMAGE, MAX_DAMAGE, charge)
	var target := _find_target()
	if target != null:
		_hit(target, damage, charge)

	end_ability(false)


func _hit(target: AbilitySystemComponent, damage: float, charge: float) -> void:
	var asc := get_ability_system_component()
	var target_body: Node3D = target.get_parent()
	var location: Vector3 = target_body.global_position + Vector3(0.0, 1.0, 0.0)

	# The effect says what is hurt and how; the spec says by how much. The
	# location rides in the context, and the Cue.Hit the effect fires lifts it out
	# to put the hit burst where the blow landed.
	var spec := asc.make_outgoing_spec(DAMAGE_EFFECT)
	spec.set_set_by_caller_magnitude(&"Damage", -damage)
	spec.context = { "location": location }
	asc.apply_gameplay_effect_spec_to_target(spec, target)

	target.send_gameplay_event(&"Event.Hit", {
		"damage": damage,
		"charge": charge,
		"instigator": get_avatar().get_path(),
	})


func _find_target() -> AbilitySystemComponent:
	var avatar: Node3D = get_avatar() as Node3D
	if avatar == null:
		return null

	var best: AbilitySystemComponent = null
	var best_distance := REACH
	for node in avatar.get_tree().get_nodes_in_group(TARGET_GROUP):
		var candidate := node as Node3D
		var target: AbilitySystemComponent = candidate.get_node_or_null(^"AbilitySystemComponent") if candidate != null else null
		if target == null:
			continue
		var distance := candidate.global_position.distance_to(avatar.global_position)
		if distance <= best_distance:
			best = target
			best_distance = distance
	return best
