extends GameplayAbility
## The training dummy's one ability. Nobody activates it: it lists Event.Hit in
## trigger_event_tags, so any hit sent to the dummy starts it, with the hit's
## payload in get_trigger_event_data().
##
## A hit that leaves health above zero is shrugged off. One that does not knocks
## the dummy down: it owns State.Down for DOWN_TIME, while which further hits
## find this ability already active and do not start it again, and then it gets
## back up at full health.

const DOWN_TIME := 2.0

var _down: GameplayTag


func _activate_ability() -> void:
	var asc := get_ability_system_component()
	if asc.get_attribute_value(&"health") > 0.0:
		end_ability(false)
		return

	if _down == null:
		_down = GameplayTag.new()
		_down.tag_name = &"State.Down"

	asc.add_loose_gameplay_tag(_down)
	await wait_delay(DOWN_TIME).completed
	asc.set_attribute_base_value(&"health", asc.get_attribute_value(&"max_health"))
	end_ability(false)


func _end_ability(_was_canceled: bool) -> void:
	# Here rather than after the await, so a cancelled knock-down still stands
	# the dummy back up.
	var asc := get_ability_system_component()
	if _down != null and asc.has_matching_gameplay_tag(_down):
		asc.remove_loose_gameplay_tag(_down)
