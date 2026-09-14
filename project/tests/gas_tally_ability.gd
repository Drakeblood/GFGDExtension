extends GameplayAbility
# Used by tests/gas_replication_probe.gd: adds exactly 1 health each time it
# runs, and ends at once. No cost, no cooldown - so nothing but the network
# rules stops it running twice for one press, which is what it is there to show.


func _activate_ability() -> void:
	var effect := GameplayEffect.new()
	var modifier := AttributeModifier.new()
	modifier.attribute = &"health"
	modifier.magnitude = 1.0
	effect.modifiers = [modifier] as Array[AttributeModifier]
	apply_effect_to_owner(effect)
	end_ability(false)
