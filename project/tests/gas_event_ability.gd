extends GameplayAbility
# Used by tests/gas_replication_probe.gd: triggered by Event.Heal, heals by the
# event's "amount". The probe sends the event on the client only - the server
# never sees it - so the server's copy heals only if the event's payload came
# with the predicted activation.


func _activate_ability() -> void:
	var asc := get_ability_system_component()

	# From a client: checked before it is trusted.
	var amount := clampi(int(get_trigger_event_data().get("amount", 0)), 0, 10)

	if asc.can_apply_effects():
		var heal := GameplayEffect.new()
		var modifier := AttributeModifier.new()
		modifier.attribute = &"health"
		modifier.magnitude = amount
		heal.modifiers = [modifier] as Array[AttributeModifier]
		apply_effect_to_owner(heal)

	end_ability(false)
