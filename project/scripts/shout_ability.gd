extends GameplayAbility
## The online demo's server-only ability. The client only asks; the server runs
## this, and everybody hears the shout through the Cue.Shout gameplay cue.
##
## The ability's own execute_gameplay_cue rather than the component's: it adds
## the shouter as source_path, and in a predicted ability it would keep the
## predicting client from hearing the cue twice.


func _activate_ability() -> void:
	execute_gameplay_cue(&"Cue.Shout", { "volume": 1.0 })
	end_ability(false)
