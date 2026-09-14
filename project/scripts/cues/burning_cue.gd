extends Node3D
## Cue.Burning: flames on the character for as long as the burn lasts.
##
## Instanced under the character when the cue is added. Having _on_removed means
## freeing is left to this script rather than done at once, so the flames can
## stop emitting and let the last of them burn out.

@onready var _flames: CPUParticles3D = $Flames
@onready var _light: OmniLight3D = $Light


func _process(_delta: float) -> void:
	_light.light_energy = 1.2 + 0.4 * sin(Time.get_ticks_msec() * 0.02)


func _on_removed(_parameters: Dictionary) -> void:
	_flames.emitting = false
	_light.visible = false
	await get_tree().create_timer(_flames.lifetime).timeout
	queue_free()
