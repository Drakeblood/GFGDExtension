extends Node3D
## Cue.Hit: a burst and a floating number where a blow landed.
##
## Executed, not added, and with a location in its parameters - so it is put in
## the world at that point rather than under the character, which would carry
## it off if the character moved. magnitude is the damage effect's, negative.

@onready var _burst: CPUParticles3D = $Burst
@onready var _number: Label3D = $Number


func _on_execute(parameters: Dictionary) -> void:
	_number.text = "%d" % roundi(absf(parameters.get("magnitude", 0.0)))
	_burst.restart()

	var tween := create_tween()
	tween.tween_property(_number, "position:y", _number.position.y + 1.0, 0.8)
	tween.parallel().tween_property(_number, "modulate:a", 0.0, 0.8)
	tween.tween_callback(queue_free)
