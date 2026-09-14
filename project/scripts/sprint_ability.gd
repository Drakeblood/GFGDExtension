extends GameplayAbility
## The online demo's predicted ability: costs stamina, has a cooldown, and
## owns State.Sprinting for a moment.
##
## LOCAL_PREDICTED, so on the owning client it runs the instant the button goes
## down - the State.Sprinting tag is there on that frame - while the server runs
## the same script, pays the cost and starts the cooldown. The client's own
## commit_ability() checks cost and cooldown against what the server last sent
## and applies nothing; the stamina and the cooldown tag arrive from the server.

const DURATION := 0.3


func _activate_ability() -> void:
	if not commit_ability():
		end_ability(true)
		return

	# If the sprint is cancelled or refused in the meantime, the task dies with
	# it and the line below never runs - no need to check get_is_active().
	await wait_delay(DURATION).completed
	end_ability(false)
