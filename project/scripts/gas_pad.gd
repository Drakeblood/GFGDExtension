extends Area3D
## A patch of floor that applies a GameplayEffect to whatever walks onto it -
## anything whose body has an AbilitySystemComponent child. Shared by the 3D
## playground and the online arena.
##
## Three ways to use one, because the effect decides how long it lasts but not
## when it is taken away again:
##   WHILE_INSIDE  applied on the way in and removed on the way out, by the id
##                 the apply returned. For an INFINITE effect - the heal pad.
##   PULSE         applied on the way in and again every pulse_interval while
##                 inside, never removed. For a stacking effect with a duration
##                 - the fire, whose burn outlasts it.
##   PICKUP        applied once, then gone for respawn_time.
##
## Effects are the server's to apply. On a client the pad does nothing, and
## what it did arrives with the character's replicated state - tags, attributes,
## the cues. A replicated client's component would refuse the effect anyway;
## asking has_authority() first only saves it the warning.

enum Mode { WHILE_INSIDE, PULSE, PICKUP }

@export var effect: GameplayEffect
@export var mode := Mode.WHILE_INSIDE
@export var pulse_interval := 0.5
@export var respawn_time := 5.0

## Per component inside: the active effect's id for WHILE_INSIDE, the time to
## the next pulse for PULSE.
var _inside := {}
var _respawn_left := 0.0


func _ready() -> void:
	body_entered.connect(_on_body_entered)
	body_exited.connect(_on_body_exited)


func is_available() -> bool:
	return _respawn_left <= 0.0


func _abilities(body: Node) -> AbilitySystemComponent:
	var asc: AbilitySystemComponent = body.get_node_or_null(^"AbilitySystemComponent")
	if asc == null or not asc.has_authority():
		return null
	return asc


func _on_body_entered(body: Node) -> void:
	var asc := _abilities(body)
	if asc == null or effect == null:
		return

	match mode:
		Mode.WHILE_INSIDE:
			_inside[asc] = asc.apply_gameplay_effect_to_self(effect)
		Mode.PULSE:
			asc.apply_gameplay_effect_to_self(effect)
			_inside[asc] = pulse_interval
		Mode.PICKUP:
			if is_available():
				asc.apply_gameplay_effect_to_self(effect)
				_respawn_left = respawn_time
				visible = false


func _on_body_exited(body: Node) -> void:
	var asc := _abilities(body)
	if asc == null or not _inside.has(asc):
		return

	if mode == Mode.WHILE_INSIDE:
		asc.remove_active_gameplay_effect(_inside[asc])
	_inside.erase(asc)


func _physics_process(delta: float) -> void:
	if _respawn_left > 0.0:
		_respawn_left -= delta
		if _respawn_left <= 0.0:
			visible = true

	if mode != Mode.PULSE:
		return

	for asc in _inside.keys():
		if not is_instance_valid(asc):
			_inside.erase(asc)
			continue
		_inside[asc] -= delta
		if _inside[asc] <= 0.0:
			asc.apply_gameplay_effect_to_self(effect)
			_inside[asc] += pulse_interval
