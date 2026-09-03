extends Level
## Prints its own lifecycle so the startup order is readable in the Output panel.
## Attached to the menu, the test level and the co-op level.


func _enter_tree() -> void:
	print("GFGD demo:      [%s] _enter_tree" % name)


func _ready() -> void:
	print("GFGD demo:      [%s] _ready" % name)


func _init_level(_world: World) -> void:
	print("GFGD demo: >> %s._init_level" % name)
