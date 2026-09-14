extends SceneTree

# Ability tasks: what an ability can await, and the promise that makes them
# worth having over a bare await - a task dies with its ability, and the code
# after an await on a dead task never runs.
#
# Every ability here records what its coroutine got to in `log`, so each check
# is "which lines ran, with what values". The cancellation checks are the point
# of the file: an ability cancelled mid-wait must not reach the line after its
# await, and the task it was waiting on must be freed, not leaked.
#
# The network half - sync_target_data carrying a client's value to the server -
# is in test_gas_replication.gd.
#
# Run with:
#   godot --headless --path project --script res://tests/test_ability_tasks.gd

var failures: Array[String] = []
var asc: AbilitySystemComponent
var log: Array = []


# --- Abilities under test ---------------------------------------------------------

class DelayAbility extends GameplayAbility:
	var log: Array
	func _activate_ability() -> void:
		log.append("start")
		await wait_delay(0.2).completed
		log.append("after delay")
		end_ability(false)


class ChargeAbility extends GameplayAbility:
	var log: Array
	func _activate_ability() -> void:
		var which: int = await wait_any([wait_input_release(), wait_delay(0.5)]).completed
		log.append(["charged", which])
		end_ability(false)


class ReleaseAbility extends GameplayAbility:
	var log: Array
	func _activate_ability() -> void:
		var held: float = await wait_input_release().completed
		log.append(["held", held])
		end_ability(false)


class TagAbility extends GameplayAbility:
	var log: Array
	func _activate_ability() -> void:
		var tag: StringName = await wait_gameplay_tag_added(&"Status.Test").completed
		log.append(["tag", tag])
		end_ability(false)


class AttributeAbility extends GameplayAbility:
	var log: Array
	func _activate_ability() -> void:
		var change: Dictionary = await wait_attribute_change(&"health").completed
		log.append(["health", change["old_value"], change["new_value"]])
		end_ability(false)


class EventAbility extends GameplayAbility:
	var log: Array
	func _activate_ability() -> void:
		var payload: Dictionary = await wait_gameplay_event(&"Event.Test").completed
		log.append(["event", payload.get("damage")])
		end_ability(false)


class AllAbility extends GameplayAbility:
	var log: Array
	func _activate_ability() -> void:
		var results: Array = await wait_all([wait_delay(0.1), wait_gameplay_event(&"Event.Test")]).completed
		log.append(["all", results[1].get("damage")])
		end_ability(false)


class SyncOfflineAbility extends GameplayAbility:
	var log: Array
	func _activate_ability() -> void:
		var data: Dictionary = await sync_target_data({ "aim": 7 }).completed
		log.append(["sync", data["aim"]])
		end_ability(false)


# Started by an event rather than a call, and filters out empty hits.
class TriggeredAbility extends GameplayAbility:
	var log: Array
	func _should_respond_to_event(_event_tag: StringName, payload: Dictionary) -> bool:
		return payload.get("damage", 1) > 0
	func _activate_ability() -> void:
		log.append(["triggered", get_trigger_event_tag(), get_trigger_event_data().get("damage")])
		end_ability(false)


# A task written in GDScript: finishes after a number of physics ticks.
class CountTicks extends AbilityTask:
	var ticks_left := 3
	func _activate() -> void:
		set_ticking(true)
	func _tick(_delta: float) -> void:
		ticks_left -= 1
		if ticks_left <= 0:
			finish("counted")


class CustomTaskAbility extends GameplayAbility:
	var log: Array
	func _activate_ability() -> void:
		var result: String = await run_task(CountTicks.new()).completed
		log.append(["custom", result])
		end_ability(false)


# --- Harness ----------------------------------------------------------------------

func _check(name: String, complaint: String) -> void:
	print("    %s %s%s" % ["PASS" if complaint.is_empty() else "FAIL", name,
		"" if complaint.is_empty() else "  -  " + complaint])
	if not complaint.is_empty():
		failures.append("%s: %s" % [name, complaint])


func _give(ability: GameplayAbility, ability_name: StringName, input := &"") -> GameplayAbility:
	ability.ability_name = ability_name
	ability.input_action_name = input
	ability.set("log", log)
	asc.give_ability(ability)
	var granted: GameplayAbility = asc.find_ability_by_name(ability_name)
	# The component grants a copy; the copy needs the same log.
	granted.set("log", log)
	return granted


func _frames(count: int) -> void:
	for i in count:
		await physics_frame


func _seconds(seconds: float) -> void:
	await _frames(int(ceil(seconds * Engine.physics_ticks_per_second)) + 1)


func _tag(tag_name: StringName) -> GameplayTag:
	var tag := GameplayTag.new()
	tag.tag_name = tag_name
	return tag


func _initialize() -> void:
	var set := AttributeSet.new()
	set.define_attribute(&"health", 100.0)
	var holder := Node.new()
	root.add_child(holder)
	asc = AbilitySystemComponent.new()
	asc.attribute_set = set
	holder.add_child(asc)
	_run.call_deferred()


func _run() -> void:
	await physics_frame

	print("=== wait_delay ===")
	var delay := _give(DelayAbility.new(), &"Delay")
	log.clear()
	asc.try_activate_ability(&"Delay")
	await _seconds(0.1)
	_check("the code before the await runs at once, the code after waits", "" if log == ["start"] else str(log))
	await _seconds(0.15)
	_check("and runs once the delay is up", "" if log == ["start", "after delay"] else str(log))
	_check("the ability's task list is empty once it ends", "" if delay.get_active_tasks().is_empty() else str(delay.get_active_tasks()))

	print("=== cancellation ===")
	log.clear()
	asc.try_activate_ability(&"Delay")
	var waiting: AbilityTask = delay.get_active_tasks()[0]
	var watcher: WeakRef = weakref(waiting)
	waiting = null
	await _seconds(0.05)
	delay.end_ability(true)
	await _seconds(0.3)
	_check("an ability cancelled mid-wait never runs the line after its await", "" if log == ["start"] else str(log))
	_check("and the task it waited on is freed, not leaked", "" if watcher.get_ref() == null else "still alive")
	asc.try_activate_ability(&"Delay")
	await _seconds(0.3)
	_check("the same ability runs normally the next time", "" if log == ["start", "start", "after delay"] else str(log))

	print("=== input ===")
	var charge := _give(ChargeAbility.new(), &"Charge", &"charge")
	log.clear()
	asc.ability_local_input_pressed(&"charge")
	await _seconds(0.1)
	asc.ability_local_input_released(&"charge")
	await _frames(2)
	_check("wait_any finishes with the first to finish - an early release", "" if log == [["charged", 0]] else str(log))
	_check("and cancels the others", "" if charge.get_active_tasks().is_empty() else str(charge.get_active_tasks()))

	log.clear()
	asc.ability_local_input_pressed(&"charge")
	await _seconds(0.6)
	_check("held past the limit, the delay wins", "" if log == [["charged", 1]] else str(log))
	asc.ability_local_input_released(&"charge")

	_give(ReleaseAbility.new(), &"Release", &"release")
	log.clear()
	asc.ability_local_input_pressed(&"release")
	await _seconds(0.3)
	asc.ability_local_input_released(&"release")
	await _frames(2)
	var held: float = log[0][1] if log.size() == 1 else -1.0
	_check("wait_input_release reports how long it was held", "" if held > 0.25 and held < 0.4 else str(log))

	print("=== tags, attributes, events ===")
	_give(TagAbility.new(), &"Tag")
	log.clear()
	asc.try_activate_ability(&"Tag")
	await _frames(2)
	_check("a tag not yet there is waited for", "" if log.is_empty() else str(log))
	asc.add_loose_gameplay_tag(_tag(&"Status.Test"))
	await _frames(1)
	_check("and resumes when it arrives", "" if log == [["tag", &"Status.Test"]] else str(log))

	log.clear()
	asc.try_activate_ability(&"Tag")
	await _frames(2)
	_check("a tag already there resumes on the next frame, not never", "" if log == [["tag", &"Status.Test"]] else str(log))
	asc.remove_loose_gameplay_tag(_tag(&"Status.Test"))

	_give(AttributeAbility.new(), &"Attribute")
	log.clear()
	asc.try_activate_ability(&"Attribute")
	asc.set_attribute_base_value(&"health", 60.0)
	await _frames(1)
	_check("wait_attribute_change gets the old and new value", "" if log == [["health", 100.0, 60.0]] else str(log))

	_give(EventAbility.new(), &"Event")
	log.clear()
	asc.try_activate_ability(&"Event")
	asc.send_gameplay_event(&"Event.Other", { "damage": 1 })
	asc.send_gameplay_event(&"Event.Test", { "damage": 12 })
	await _frames(1)
	_check("wait_gameplay_event resumes on its event only, with the payload", "" if log == [["event", 12]] else str(log))

	_give(AllAbility.new(), &"All")
	log.clear()
	asc.try_activate_ability(&"All")
	asc.send_gameplay_event(&"Event.Test", { "damage": 5 })
	await _frames(2)
	_check("wait_all waits for the slowest", "" if log.is_empty() else str(log))
	await _seconds(0.15)
	_check("and hands back every result in order", "" if log == [["all", 5]] else str(log))

	print("=== offline sync and custom tasks ===")
	_give(SyncOfflineAbility.new(), &"Sync")
	log.clear()
	asc.try_activate_ability(&"Sync")
	await _frames(1)
	_check("with no network sync_target_data returns its own data", "" if log == [["sync", 7]] else str(log))

	_give(CustomTaskAbility.new(), &"Custom")
	log.clear()
	asc.try_activate_ability(&"Custom")
	await _frames(2)
	_check("a GDScript task ticks", "" if log.is_empty() else str(log))
	await _frames(3)
	_check("and finishes with its own result", "" if log == [["custom", "counted"]] else str(log))

	print("=== event triggers ===")
	var triggered := TriggeredAbility.new()
	triggered.trigger_event_tags = GameplayTagContainer.new()
	triggered.trigger_event_tags.set_tags(["Event.Hit"])
	_give(triggered, &"Triggered")
	log.clear()
	asc.send_gameplay_event(&"Event.Hit.Head", { "damage": 3 })
	_check("an event activates an ability it triggers - a child tag included, with the payload",
		"" if log == [["triggered", &"Event.Hit.Head", 3]] else str(log))
	log.clear()
	asc.send_gameplay_event(&"Event.Other", { "damage": 3 })
	_check("and not one it does not trigger", "" if log.is_empty() else str(log))
	asc.send_gameplay_event(&"Event.Hit", { "damage": 0 })
	_check("_should_respond_to_event can turn an event down", "" if log.is_empty() else str(log))
	asc.try_activate_ability(&"Triggered")
	_check("an activation nothing triggered carries no event", "" if log == [["triggered", &"", null]] else str(log))

	var idle: GameplayAbility = asc.find_ability_by_name(&"Delay")
	var orphan: AbilityTask = idle.wait_delay(0.1)
	_check("a task started by an inactive ability comes back cancelled", "" if orphan.is_cancelled() else "it runs with nothing to end it")

	if failures.is_empty():
		print("test_ability_tasks: all checks passed")
		quit(0)
	else:
		print("test_ability_tasks: %d check(s) failed" % failures.size())
		quit(1)
