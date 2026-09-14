extends GameplayAbility
# Used by tests/gas_replication_probe.gd. Reads an "aim" off the component's
# metadata - the probe sets it on the client only - and heals by that much.
#
# sync_target_data is what makes the server heal by the client's number rather
# than its own zero: on the predicting client it sends the value and resumes at
# once; on the server's copy it resumes with what the client sent.


func _activate_ability() -> void:
	var asc := get_ability_system_component()
	var data: Dictionary = await sync_target_data({ "aim": int(asc.get_meta("aim", 0)) }).completed

	# From a client: checked before it is trusted.
	var aim := clampi(int(data.get("aim", 0)), 0, 10)

	if asc.can_apply_effects():
		var heal := GameplayEffect.new()
		var modifier := AttributeModifier.new()
		modifier.attribute = &"health"
		modifier.magnitude = aim
		heal.modifiers = [modifier] as Array[AttributeModifier]
		apply_effect_to_owner(heal)

	end_ability(false)
