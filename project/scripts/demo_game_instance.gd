extends GameInstance
## Demo GameInstance: lives for the whole application lifetime.


func _on_init(_scene_tree: GFGDSceneTree) -> void:
	print("GFGD demo: >> GameInstance._on_init")


func _on_shutdown() -> void:
	print("GFGD demo: >> GameInstance._on_shutdown")
