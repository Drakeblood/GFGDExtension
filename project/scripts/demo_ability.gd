extends GameplayAbility
## Demo ability: applies an instant damage effect to the owner and ends itself.

const DAMAGE_EFFECT := preload("res://resources/damage_effect.tres")


func _activate_ability() -> void:
	print("GFGD demo: TestAbility activated")
	apply_effect_to_owner(DAMAGE_EFFECT)
	end_ability(false)


func _end_ability(was_canceled: bool) -> void:
	print("GFGD demo: TestAbility ended (canceled=%s)" % was_canceled)
