extends SceneTree

# The 3D playground's ability yard, station by station.
#
# The yard is the gameplay ability system on a map: pads that apply effects,
# a pickup, a strike and a dummy to take it. Like the movement stations, it is
# only worth having while each sign still tells the truth, and the ways it can
# stop doing so are quiet ones - a pad whose volume no longer reaches the
# capsule, a burn that stops stacking, a cue table dropped from the project
# settings so nothing burns on screen any more, a dummy that stays down.
#
# The real level and the real player pawn are instantiated, with no World: the
# level's attach_to() stands in for player_restarted, and the strike is pressed
# straight on the component, which is what the pawn's input binding does.
#
# Run with:
#   godot --headless --path project --script res://tests/test_gas_playground.gd

var failures: Array[String] = []
var level: Node
var body: CharacterBody3D
var pawn: Pawn
var cmc: CharacterMovementComponent
var asc: AbilitySystemComponent
var dummy: Node3D
var dummy_asc: AbilitySystemComponent

## Every Cue.Hit the dummy plays. Counted from the signal, not from the burst
## nodes, which free themselves after a moment.
var dummy_hits := 0


func _check(name: String, complaint: String) -> void:
	print("    %s %s%s" % ["PASS" if complaint.is_empty() else "FAIL", name,
		"" if complaint.is_empty() else "  -  " + complaint])
	if not complaint.is_empty():
		failures.append("%s: %s" % [name, complaint])


func _frames(count: int) -> void:
	for i in count:
		await physics_frame


func _seconds(seconds: float) -> void:
	await _frames(int(ceil(seconds * Engine.physics_ticks_per_second)))


func _tag(tag_name: StringName) -> GameplayTag:
	var tag := GameplayTag.new()
	tag.tag_name = tag_name
	return tag


func _has(component: AbilitySystemComponent, tag_name: StringName) -> bool:
	return component.get_gameplay_tag_count(_tag(tag_name)) > 0


func _effect(component: AbilitySystemComponent, file: String) -> ActiveGameplayEffect:
	for active: ActiveGameplayEffect in component.get_active_effects():
		if active.get_effect().resource_path.get_file() == file:
			return active
	return null


func _dummy_label() -> Label3D:
	return dummy.find_children("*", "Label3D", false, false)[0]


## Cue instances, by the scene they came from - "HealCue" is heal_cue.tscn. Not
## by name: a second instance under the same parent is renamed "@Node3D@114",
## and it is exactly the second instances that must not pile up.
func _children_named(parent: Node, scene_name: String) -> Array:
	var file := scene_name.to_snake_case() + ".tscn"
	return parent.get_children().filter(func(child: Node) -> bool: return child.scene_file_path.get_file() == file)


## Puts the character down somewhere, still, and lets the areas notice.
func _teleport(to: Vector3) -> void:
	body.position = to
	cmc.set_velocity(Vector3.ZERO)
	cmc.set_movement_mode(&"Walking")
	await _frames(2)


func _initialize() -> void:
	level = load("res://levels/test_level.tscn").instantiate()
	root.add_child(level)

	body = load("res://scenes/player_pawn.tscn").instantiate()
	body.position = Vector3(0.0, 1.02, 0.0)
	root.add_child(body)

	pawn = body.get_node("Pawn")
	pawn.replicate_transform = false
	cmc = body.get_node("CharacterMovementComponent")
	asc = body.get_node("AbilitySystemComponent")
	_run.call_deferred()


func _run() -> void:
	await _frames(2)
	level.attach_to(cmc)

	print("=== the yard ===")
	dummy = level.get_node_or_null("TrainingDummy")
	dummy_asc = dummy.get_node_or_null("AbilitySystemComponent") if dummy != null else null
	var missing: Array = ["HealPad", "FirePad", "HastePickup", "TrainingDummy"].filter(
		func(node_name: String) -> bool: return level.get_node_or_null(node_name) == null)
	_check("every station is built", "" if missing.is_empty() else "missing %s" % str(missing))
	if dummy_asc != null:
		dummy_asc.gameplay_cue.connect(func(cue_tag: StringName, _event: int, _parameters: Dictionary) -> void:
			if cue_tag == &"Cue.Hit":
				dummy_hits += 1)
	_check("the dummy has a component of its own, and is where the strike looks",
		"" if dummy_asc != null and dummy.is_in_group(&"gas_targets") else "no component, or not in gas_targets")
	_check("the player starts at full health, mana and walking speed",
		"" if asc.get_attribute_value(&"health") == 100.0 and asc.get_attribute_value(&"mana") == 50.0 \
			and asc.get_attribute_value(&"move_speed") == 6.0 else "health %s mana %s move_speed %s" % [
			asc.get_attribute_value(&"health"), asc.get_attribute_value(&"mana"), asc.get_attribute_value(&"move_speed")])
	_check("the strike is granted", "" if asc.find_ability_by_name(&"Strike") != null else "no Strike ability")

	await _fire()
	await _heal()
	await _haste()
	await _strike()
	await _hud()

	if failures.is_empty():
		print("test_gas_playground: all checks passed")
		quit(0)
	else:
		print("test_gas_playground: %d FAILED" % failures.size())
		for failure in failures:
			print("  - %s" % failure)
		quit(1)


func _fire() -> void:
	print("=== 9b fire ===")
	var fire_pad: Node3D = level.get_node("FirePad")
	await _teleport(fire_pad.position + Vector3(0.0, 1.02, 0.0))
	_check("stepping into the fire sets you burning",
		"" if _has(asc, &"State.Burning") and _effect(asc, "burning.tres") != null else "no State.Burning")
	_check("with flames on the character - the cue table in the project settings is read",
		"" if _children_named(body, "BurningCue").size() == 1 else "%d BurningCue nodes" % _children_named(body, "BurningCue").size())

	await _seconds(1.5)
	var burn := _effect(asc, "burning.tres")
	_check("standing in it stacks the burn to its limit of 3",
		"" if burn != null and burn.get_stack_count() == 3 else "stacks %s" % (burn.get_stack_count() if burn != null else 0))
	_check("the burn's period ticks do not pile up flames - one lasting instance",
		"" if _children_named(body, "BurningCue").size() == 1 else "%d BurningCue nodes" % _children_named(body, "BurningCue").size())
	var health_in_fire := asc.get_attribute_value(&"health")
	_check("and it hurts", "" if health_in_fire < 100.0 else "health %s" % health_in_fire)

	await _teleport(Vector3(-3.0, 1.02, 6.5))
	await _seconds(0.6)
	_check("the burn outlasts the fire", "" if _has(asc, &"State.Burning") and asc.get_attribute_value(&"health") < health_in_fire \
		else "burning %s health %s" % [_has(asc, &"State.Burning"), asc.get_attribute_value(&"health")])
	await _seconds(2.0)
	burn = _effect(asc, "burning.tres")
	_check("and wears off a stack at a time", "" if burn != null and burn.get_stack_count() < 3 \
		else "stacks %s" % (burn.get_stack_count() if burn != null else 0))
	await _seconds(4.5)
	await _frames(2)
	_check("until it is gone, and the flames with it", "" if not _has(asc, &"State.Burning") and _children_named(body, "BurningCue").is_empty() \
		else "burning %s, %d BurningCue nodes" % [_has(asc, &"State.Burning"), _children_named(body, "BurningCue").size()])


func _heal() -> void:
	print("=== 9a heal pad ===")
	var hurt := asc.get_attribute_value(&"health")
	var heal_pad: Node3D = level.get_node("HealPad")
	await _teleport(heal_pad.position + Vector3(0.0, 1.02, 0.0))
	_check("standing on the pad heals", "" if _has(asc, &"State.Healing") and _children_named(body, "HealCue").size() == 1 \
		else "healing %s, %d HealCue nodes" % [_has(asc, &"State.Healing"), _children_named(body, "HealCue").size()])
	await _seconds(1.1)
	_check("the heal's period ticks do not pile up sparkles - one lasting instance",
		"" if _children_named(body, "HealCue").size() == 1 else "%d HealCue nodes" % _children_named(body, "HealCue").size())
	_check("+4 every half second", "" if is_equal_approx(asc.get_attribute_value(&"health"), minf(hurt + 8.0, 100.0)) \
		else "%s -> %s" % [hurt, asc.get_attribute_value(&"health")])

	await _seconds(12.0)
	_check("and stops at max_health, the base included - no hidden overheal banked",
		"" if asc.get_attribute_value(&"health") == 100.0 and asc.get_attribute_base_value(&"health") == 100.0 \
			else "current %s base %s" % [asc.get_attribute_value(&"health"), asc.get_attribute_base_value(&"health")])

	await _teleport(Vector3(-8.0, 1.02, 6.5))
	await _frames(2)
	_check("stepping off removes the effect, its tag and its cue",
		"" if _effect(asc, "heal_zone.tres") == null and not _has(asc, &"State.Healing") and _children_named(body, "HealCue").is_empty() \
			else "effect %s tag %s" % [_effect(asc, "heal_zone.tres"), _has(asc, &"State.Healing")])


func _haste() -> void:
	print("=== 9c haste ===")
	var pickup: Node3D = level.get_node("HastePickup")
	await _teleport(Vector3(pickup.position.x, 1.02, pickup.position.z))
	_check("the pickup hastes", "" if _has(asc, &"State.Hasted") and is_equal_approx(asc.get_attribute_value(&"move_speed"), 7.5) \
		else "hasted %s move_speed %s" % [_has(asc, &"State.Hasted"), asc.get_attribute_value(&"move_speed")])
	_check("and the pawn turns move_speed into walking speed",
		"" if is_equal_approx(cmc.max_walk_speed, 7.5) else "max_walk_speed %s" % cmc.max_walk_speed)
	var mesh: MeshInstance3D = body.get_node("Mesh")
	_check("the haste cue - a notify resource - tints the character", "" if mesh.material_overlay != null else "no overlay")
	_check("and the pickup is gone until it respawns", "" if not pickup.visible and not pickup.is_available() else "still there")

	await _teleport(Vector3(-10.0, 1.02, 11.0))
	for i in 90:
		pawn.add_movement_input(Vector3(1, 0, 0))
		await physics_frame
	_check("hasted, the character walks at 7.5 m/s", "" if absf(cmc.get_velocity().length() - 7.5) < 0.2 \
		else "speed %.2f" % cmc.get_velocity().length())

	await _seconds(6.0)
	_check("when it runs out, back to 6 and untinted",
		"" if cmc.max_walk_speed == 6.0 and mesh.material_overlay == null and pickup.is_available() and pickup.visible \
			else "max_walk_speed %s overlay %s pickup back %s" % [cmc.max_walk_speed, mesh.material_overlay, pickup.is_available()])


func _strike() -> void:
	print("=== 9d strike and dummy ===")
	await _teleport(dummy.position + Vector3(-1.8, 0.02, 0.0))
	await _seconds(3.0)    # mana back to full

	var mana := asc.get_attribute_value(&"mana")
	asc.ability_local_input_pressed(&"strike")
	await _frames(2)
	_check("holding Q charges, with a glow on the character",
		"" if asc.find_ability_by_name(&"Strike").get_is_active() and _children_named(body, "ChargingCue").size() == 1 \
			else "%d ChargingCue nodes" % _children_named(body, "ChargingCue").size())
	await _seconds(0.5)
	asc.ability_local_input_released(&"strike")
	await _frames(2)

	var health := dummy_asc.get_attribute_value(&"health")
	_check("letting go half way hits for about half way between 10 and 40",
		"" if health > 71.0 and health < 78.0 else "dummy health %s" % health)
	_check("the dummy's label follows its health - an AbilityAsync on the component",
		"" if _dummy_label().text == "%.0f / 100" % health else "label %s" % _dummy_label().text)
	# 10 for the strike, less the one regen tick (+3) the half second of charging spans.
	_check("the strike cost 10 mana", "" if is_equal_approx(asc.get_attribute_value(&"mana"), mana - 7.0) else "%s -> %s" % [mana, asc.get_attribute_value(&"mana")])
	_check("and the glow is gone once the ability ends", "" if _children_named(body, "ChargingCue").is_empty() else "still glowing")
	var bursts := _children_named(level, "HitCue")
	_check("the hit cue plays where the blow landed, in the world",
		"" if bursts.size() == 1 and bursts[0].global_position.distance_to(dummy.global_position + Vector3(0, 1, 0)) < 0.01 \
			else "%d HitCue nodes under the level" % bursts.size())
	_check("with the damage on it", "" if bursts.size() == 1 and bursts[0].get_node("Number").text == str(roundi(100.0 - health)) \
		else "text %s" % (bursts[0].get_node("Number").text if bursts.size() == 1 else "-"))

	asc.ability_local_input_pressed(&"strike")
	asc.ability_local_input_released(&"strike")
	await _frames(2)
	_check("a second strike inside the cooldown is refused",
		"" if dummy_asc.get_attribute_value(&"health") == health and _has(asc, &"Cooldown.Strike") else "dummy health %s" % dummy_asc.get_attribute_value(&"health"))

	# Full charges until it goes down: held past the limit, each goes off by
	# itself. Two do it from about 75; a third is allowed in case the numbers move.
	for i in 3:
		await _seconds(0.7)
		asc.ability_local_input_pressed(&"strike")
		await _seconds(1.1)
		asc.ability_local_input_released(&"strike")
		await _frames(2)
		if _has(dummy_asc, &"State.Down"):
			break
	_check("at 0 health the dummy's HitReact - triggered by Event.Hit - knocks it down",
		"" if dummy_asc.get_attribute_value(&"health") == 0.0 and _has(dummy_asc, &"State.Down") \
			else "health %s down %s" % [dummy_asc.get_attribute_value(&"health"), _has(dummy_asc, &"State.Down")])
	await _frames(1)
	_check("and the label and the pose show it", "" if _dummy_label().text == "DOWN" and dummy.get_node("Mesh").rotation.z != 0.0 \
		else "label %s" % _dummy_label().text)

	var hits_before := dummy_hits
	await _seconds(0.3)
	asc.ability_local_input_pressed(&"strike")
	asc.ability_local_input_released(&"strike")
	await _frames(2)
	_check("a dummy that is down takes no damage and plays no hit",
		"" if dummy_hits == hits_before else "a hit landed")

	await _seconds(1.8)
	_check("and gets back up at full health", "" if not _has(dummy_asc, &"State.Down") and dummy_asc.get_attribute_value(&"health") == 100.0 \
		else "health %s down %s" % [dummy_asc.get_attribute_value(&"health"), _has(dummy_asc, &"State.Down")])
	_check("standing, with its label back", "" if _dummy_label().text == "100 / 100" and dummy.get_node("Mesh").rotation.z == 0.0 \
		else "label %s" % _dummy_label().text)

	await _teleport(dummy.position + Vector3(-6.0, 0.02, 0.0))
	health = dummy_asc.get_attribute_value(&"health")
	await _seconds(0.7)
	asc.ability_local_input_pressed(&"strike")
	asc.ability_local_input_released(&"strike")
	await _frames(2)
	_check("out of reach, a strike hits nothing", "" if dummy_asc.get_attribute_value(&"health") == health else "health %s" % dummy_asc.get_attribute_value(&"health"))


func _hud() -> void:
	print("=== the HUD ===")
	var hud: Label = level.get_node("UI/HUD")
	await process_frame
	_check("the HUD shows the character's attributes, tags and effects",
		"" if hud.text.contains("health") and hud.text.contains("mana") and hud.text.contains("effects") and hud.text.contains("mana_regen") \
			else hud.text)
