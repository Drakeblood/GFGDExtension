extends AttributeSet
## The demo's attributes, kept in range: health and mana between 0 and their
## max_, move_speed never below 1.
##
## Two halves, because an effect can move either value. _pre_attribute_change
## clamps the current value - what a buff on top of the base comes to. The base
## is clamped as it changes, so a heal pad that ticks for a minute does not bank
## two hundred health under a bar that reads 100 and then soak up the next ten
## hits unseen. Connected in _init: the component plays with a copy of this set,
## and a copy runs _init again, where a connection made in the editor would not
## come along.
##
## Only the server runs this; a client is sent values that are already clamped.


func _init() -> void:
	base_value_changed.connect(_clamp_base)


func _pre_attribute_change(attribute_name: StringName, new_value: float) -> float:
	return _clamped(attribute_name, new_value)


func _clamp_base(attribute_name: StringName, _old_value: float, new_value: float) -> void:
	var clamped := _clamped(attribute_name, new_value)
	if clamped != new_value:
		set_base_value(attribute_name, clamped)


func _clamped(attribute_name: StringName, value: float) -> float:
	match attribute_name:
		&"health":
			return clampf(value, 0.0, get_base_value(&"max_health"))
		&"mana":
			return clampf(value, 0.0, get_base_value(&"max_mana"))
		&"move_speed":
			return maxf(value, 1.0)
	return value
