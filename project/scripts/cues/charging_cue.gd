extends Node3D
## Cue.Charging: a glow that swells while the strike is held. No _on_removed, so
## it is freed the moment the strike lets go.

const FULL := 1.0

var _held := 0.0


func _process(delta: float) -> void:
	_held = minf(_held + delta, FULL)
	scale = Vector3.ONE * lerpf(0.3, 1.0, _held / FULL)
