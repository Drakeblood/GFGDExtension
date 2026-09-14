extends SceneTree

# Gameplay cue handlers: a cue tag resolved to what plays it.
#
# A GameplayCueNotify resource is called for each event with the character as
# target; a PackedScene is instanced - under the character while an added cue
# lasts, freed (or asked to fade out) when it is removed, and placed in the
# world when an executed cue says where. A tag with no handler of its own uses
# its parent's. Cues from effects carry level, magnitude and source; a cue an
# ability adds is removed when the ability ends.
#
# The network half - a predicted ability's cue heard once, not twice, by the
# client that predicted it - is in test_gas_replication.gd.
#
# Run with:
#   godot --headless --path project --script res://tests/test_gameplay_cues.gd

var failures: Array[String] = []
var asc: AbilitySystemComponent
var avatar: Node3D
var world_root: Node3D

# Written by the handlers below, which reach it through Engine metadata - a
# script run with --script has no class_name to be found by.
var calls: Array = []


class LoggingNotify extends GameplayCueNotify:
	func _on_execute(target: Node, parameters: Dictionary) -> void:
		Engine.get_meta(&"cue_calls").append(["execute", target.name, parameters])
	func _on_active(target: Node, parameters: Dictionary) -> void:
		Engine.get_meta(&"cue_calls").append(["active", target.name, parameters])
	func _on_removed(target: Node, _parameters: Dictionary) -> void:
		Engine.get_meta(&"cue_calls").append(["removed", target.name])


class CueAbility extends GameplayAbility:
	func _activate_ability() -> void:
		add_gameplay_cue(&"Cue.Burning")


const SCENE_SCRIPT := """
extends Node3D
func _on_active(parameters: Dictionary) -> void:
	Engine.get_meta(&"cue_calls").append(["scene active", get_parent().name])
func _on_execute(parameters: Dictionary) -> void:
	Engine.get_meta(&"cue_calls").append(["scene execute", get_parent().name, global_position])
	queue_free()
func _on_removed(parameters: Dictionary) -> void:
	Engine.get_meta(&"cue_calls").append(["scene removed"])
	queue_free()
"""


# A lasting cue that also hears its effect's period ticks, and stays put.
const LOOP_SCRIPT := """
extends Node3D
func _on_execute(parameters: Dictionary) -> void:
	Engine.get_meta(&"cue_calls").append(["loop tick", parameters.get("n")])
"""


func _check(name: String, complaint: String) -> void:
	print("    %s %s%s" % ["PASS" if complaint.is_empty() else "FAIL", name,
		"" if complaint.is_empty() else "  -  " + complaint])
	if not complaint.is_empty():
		failures.append("%s: %s" % [name, complaint])


func _scene(with_script: bool, source := SCENE_SCRIPT) -> PackedScene:
	var node := Node3D.new()
	node.name = "CueNode"
	if with_script:
		var script := GDScript.new()
		script.source_code = source
		script.reload()
		node.set_script(script)
	var scene := PackedScene.new()
	scene.pack(node)
	node.free()
	return scene


# By type, not name: a second instance under the same parent is renamed
# "@Node3D@12", and the second instances are what must not pile up.
func _cue_nodes(parent: Node) -> Array:
	return parent.get_children().filter(func(child: Node) -> bool: return child is Node3D)


func _initialize() -> void:
	Engine.set_meta(&"cue_calls", calls)

	world_root = Node3D.new()
	world_root.name = "WorldRoot"
	root.add_child(world_root)
	avatar = Node3D.new()
	avatar.name = "Avatar"
	world_root.add_child(avatar)

	var set := AttributeSet.new()
	set.define_attribute(&"health", 100.0)
	asc = AbilitySystemComponent.new()
	asc.attribute_set = set
	avatar.add_child(asc)

	var table := GameplayCueTable.new()
	table.cues = {
		&"Cue.Test": LoggingNotify.new(),
		&"Cue.Burning": _scene(true),
		&"Cue.Hit": _scene(true),
		&"Cue.Plain": _scene(false),
		&"Cue.Loop": _scene(true, LOOP_SCRIPT),
	}
	GameplayCueManager.get_singleton().clear()
	GameplayCueManager.get_singleton().add_table(table)
	_run.call_deferred()


func _run() -> void:
	await physics_frame

	print("=== resolution ===")
	var manager := GameplayCueManager.get_singleton()
	_check("a tag with a handler resolves to it", "" if manager.find_handler(&"Cue.Test") is LoggingNotify else "no handler")
	_check("a child tag without one falls back to its parent's", "" if manager.find_handler(&"Cue.Test.Child") is LoggingNotify else "no handler")
	_check("an unknown tag has none", "" if manager.find_handler(&"Cue.Unknown") == null else "found one")

	print("=== a notify resource ===")
	calls.clear()
	asc.execute_gameplay_cue_by_name(&"Cue.Test.Child", { "x": 1 })
	_check("an executed cue calls _on_execute with the character as target",
		"" if calls.size() == 1 and calls[0][0] == "execute" and calls[0][1] == &"Avatar" and calls[0][2].get("x") == 1 else str(calls))
	calls.clear()
	asc.add_gameplay_cue_by_name(&"Cue.Test")
	asc.add_gameplay_cue_by_name(&"Cue.Test")
	asc.remove_gameplay_cue_by_name(&"Cue.Test")
	_check("an added cue calls _on_active once, however many sources add it", "" if calls.size() == 1 and calls[0][0] == "active" else str(calls))
	asc.remove_gameplay_cue_by_name(&"Cue.Test")
	_check("and _on_removed when the last source removes it", "" if calls.size() == 2 and calls[1][0] == "removed" else str(calls))

	print("=== a scene ===")
	calls.clear()
	asc.add_gameplay_cue_by_name(&"Cue.Burning")
	_check("an added cue's scene is instanced under the character", "" if _cue_nodes(avatar).size() == 1 and calls == [["scene active", &"Avatar"]] else str(calls))
	asc.remove_gameplay_cue_by_name(&"Cue.Burning")
	await process_frame
	_check("removed, it is told so and frees itself", "" if _cue_nodes(avatar).is_empty() and calls.has(["scene removed"]) else "%s, %d nodes" % [str(calls), _cue_nodes(avatar).size()])

	asc.add_gameplay_cue_by_name(&"Cue.Plain")
	_check("a scene with no script is instanced as well", "" if _cue_nodes(avatar).size() == 1 else "%d nodes" % _cue_nodes(avatar).size())
	asc.remove_gameplay_cue_by_name(&"Cue.Plain")
	await process_frame
	_check("and freed on removal", "" if _cue_nodes(avatar).is_empty() else "%d nodes" % _cue_nodes(avatar).size())

	calls.clear()
	asc.execute_gameplay_cue_by_name(&"Cue.Hit", { "location": Vector3(3, 0, 4) })
	_check("an executed cue with a location goes into the world at that point",
		"" if calls.size() == 1 and calls[0][1] == &"WorldRoot" and calls[0][2].is_equal_approx(Vector3(3, 0, 4)) else str(calls))

	print("=== executed while added - a periodic effect's ticks ===")
	calls.clear()
	asc.add_gameplay_cue_by_name(&"Cue.Loop")
	asc.execute_gameplay_cue_by_name(&"Cue.Loop", { "n": 1 })
	asc.execute_gameplay_cue_by_name(&"Cue.Loop", { "n": 2 })
	_check("an execute of a cue already running goes to that instance, not a new one",
		"" if _cue_nodes(avatar).size() == 1 and calls == [["loop tick", 1], ["loop tick", 2]] else "%s, %d nodes" % [str(calls), _cue_nodes(avatar).size()])
	asc.remove_gameplay_cue_by_name(&"Cue.Loop")
	await process_frame
	_check("and removing it leaves nothing behind", "" if _cue_nodes(avatar).is_empty() else "%d nodes" % _cue_nodes(avatar).size())

	asc.add_gameplay_cue_by_name(&"Cue.Plain")
	asc.execute_gameplay_cue_by_name(&"Cue.Plain")
	asc.execute_gameplay_cue_by_name(&"Cue.Plain")
	_check("a running cue with no _on_execute is not duplicated either", "" if _cue_nodes(avatar).size() == 1 else "%d nodes" % _cue_nodes(avatar).size())
	asc.remove_gameplay_cue_by_name(&"Cue.Plain")
	await process_frame

	# An executed scene has to free itself, and one with no _on_execute cannot:
	# freed at once (with a warning), not kept for good.
	asc.execute_gameplay_cue_by_name(&"Cue.Plain")
	await process_frame
	_check("an executed scene with no _on_execute is freed, not kept for good", "" if _cue_nodes(avatar).is_empty() else "%d nodes" % _cue_nodes(avatar).size())

	print("=== from effects ===")
	calls.clear()
	var hit := GameplayEffect.new()
	var modifier := AttributeModifier.new()
	modifier.attribute = &"health"
	modifier.magnitude = -7.0
	hit.modifiers = [modifier] as Array[AttributeModifier]
	hit.gameplay_cue_tags = GameplayTagContainer.new()
	hit.gameplay_cue_tags.set_tags(["Cue.Test"])
	var spec := asc.make_outgoing_spec(hit, 3.0)
	spec.context = { "location": Vector3(1, 2, 3) }
	asc.apply_gameplay_effect_spec_to_self(spec)
	var parameters: Dictionary = calls[0][2] if calls.size() == 1 else {}
	_check("an effect's cue carries level, magnitude and source",
		"" if parameters.get("level") == 3.0 and parameters.get("magnitude") == -7.0 and parameters.get("source_path") == avatar.get_path() else str(parameters))
	_check("and lifts location out of the spec's context", "" if parameters.get("location") == Vector3(1, 2, 3) else str(parameters))

	print("=== from abilities ===")
	var ability := CueAbility.new()
	ability.ability_name = &"CueAbility"
	asc.give_ability(ability)
	asc.try_activate_ability(&"CueAbility")
	_check("a cue an ability adds is running while it is active", "" if asc.is_gameplay_cue_active(&"Cue.Burning") else "not active")
	asc.find_ability_by_name(&"CueAbility").end_ability(false)
	await process_frame
	_check("and removed when the ability ends", "" if not asc.is_gameplay_cue_active(&"Cue.Burning") and _cue_nodes(avatar).is_empty() else "still running")

	if failures.is_empty():
		print("test_gameplay_cues: all checks passed")
		quit(0)
	else:
		print("test_gameplay_cues: %d check(s) failed" % failures.size())
		quit(1)
