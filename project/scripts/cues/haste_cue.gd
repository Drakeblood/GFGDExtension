extends GameplayCueNotify
## Cue.Haste: tints the character while hasted.
##
## A GameplayCueNotify resource rather than a scene: nothing is instanced, the
## handler is handed the character and changes it. Shared by every character
## the cue plays on, so it keeps no state of its own.

const TINT := preload("res://resources/gas/haste_tint.tres")


func _on_active(target: Node, _parameters: Dictionary) -> void:
	var mesh: MeshInstance3D = target.get_node_or_null(^"Mesh")
	if mesh != null:
		mesh.material_overlay = TINT


func _on_removed(target: Node, _parameters: Dictionary) -> void:
	var mesh: MeshInstance3D = target.get_node_or_null(^"Mesh")
	if mesh != null:
		mesh.material_overlay = null
