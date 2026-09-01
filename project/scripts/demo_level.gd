extends Level
## Demo Level: prints its own lifecycle so the startup order is readable in the
## Output panel. Attached to both MainMenu and TestLevel.


func _enter_tree() -> void:
	print("GFGD demo:      [%s] _enter_tree" % name)


func _ready() -> void:
	print("GFGD demo:      [%s] _ready" % name)


func _init_level(_scene_tree: GFGDSceneTree) -> void:
	print("GFGD demo: >> %s._init_level" % name)
