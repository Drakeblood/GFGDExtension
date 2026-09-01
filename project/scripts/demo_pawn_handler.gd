extends PawnHandler
## Demo PawnHandler: binds input actions to the AbilitySystemComponent
## when a PlayerController possesses this pawn, and prints its own lifecycle
## so it can be lined up against the level's.

@export var tag: GameplayTag
@export var tags: GameplayTagContainer


func _enter_tree() -> void:
	print("GFGD demo:      [Player/%s] _enter_tree" % name)


func _ready() -> void:
	print("GFGD demo:      [Player/%s] _ready" % name)


func _possessed(controller: Controller) -> void:
	print("GFGD demo: >> Player possessed by %s" % controller.get_class())


func _setup_input_component(input_component: InputComponent) -> void:
	print("GFGD demo: >> Player._setup_input_component")
	var asc: AbilitySystemComponent = get_pawn_root().get_node("AbilitySystemComponent")
	input_component.bind_action(&"activate_test", InputComponent.STARTED,
		func() -> void: asc.ability_local_input_pressed(&"activate_test"))
	input_component.bind_action(&"activate_test", InputComponent.COMPLETED,
		func() -> void: asc.ability_local_input_released(&"activate_test"))
