extends SceneTree

# AbilityAsync: waiting on an AbilitySystemComponent from outside any ability -
# a HUD, a quest, a door. Unreal's UAbilityAsync.
#
# What it promises, and what each section holds it to: it lives without anyone
# keeping a variable for it (the target holds it); it triggers on every change,
# or once; something already true when it starts counts, but is delivered only
# once the caller has had the chance to connect; and it ends - never to trigger
# again, releasing whatever awaits it, freed rather than leaked - when told to,
# when its owner node leaves the tree or is freed, or when its target is freed.
#
# Run with:
#   godot --headless --path project --script res://tests/test_ability_async.gd

var failures: Array[String] = []
var asc: AbilitySystemComponent
var holder: Node
var log: Array = []


# A GDScript async of its own: triggers when an ability activates on the target.
class WaitActivation extends AbilityAsync:
	func _activate() -> void:
		get_ability_system_component().ability_activated.connect(_on_activated)
	func _on_end() -> void:
		var target := get_ability_system_component()
		if target != null and target.ability_activated.is_connected(_on_activated):
			target.ability_activated.disconnect(_on_activated)
	func _on_activated(ability: GameplayAbility) -> void:
		broadcast(ability.ability_name)


func _check(name: String, complaint: String) -> void:
	print("    %s %s%s" % ["PASS" if complaint.is_empty() else "FAIL", name,
		"" if complaint.is_empty() else "  -  " + complaint])
	if not complaint.is_empty():
		failures.append("%s: %s" % [name, complaint])


func _frames(count: int) -> void:
	for i in count:
		await process_frame


func _tag(tag_name: StringName) -> GameplayTag:
	var tag := GameplayTag.new()
	tag.tag_name = tag_name
	return tag


func _effect(tags: PackedStringArray) -> GameplayEffect:
	var effect := GameplayEffect.new()
	effect.duration_policy = GameplayEffect.INFINITE
	effect.effect_tags = GameplayTagContainer.new()
	effect.effect_tags.set_tags(tags)
	return effect


func _make_asc() -> AbilitySystemComponent:
	var set := AttributeSet.new()
	set.define_attribute(&"health", 100.0)
	var component := AbilitySystemComponent.new()
	component.attribute_set = set
	holder.add_child(component)
	return component


# Awaits an async and records whether the line after the await ran.
func _await_and_log(async: AbilityAsync, label: String) -> void:
	var result = await async.triggered
	log.append([label, result])


func _initialize() -> void:
	holder = Node.new()
	root.add_child(holder)
	asc = _make_asc()
	_run.call_deferred()


func _run() -> void:
	await _frames(1)

	print("=== repeating, and kept alive by the target ===")
	log.clear()
	AbilityAsync.wait_attribute_change(asc, &"health").triggered.connect(func(change: Dictionary) -> void:
		log.append([change["old_value"], change["new_value"]]))
	asc.set_attribute_base_value(&"health", 80.0)
	asc.set_attribute_base_value(&"health", 60.0)
	_check("an async nobody kept a variable for still triggers, on every change",
		"" if log == [[100.0, 80.0], [80.0, 60.0]] else str(log))
	# Found again the way a caller that did not keep it would have to: by what
	# it watches. Ended, so it stays out of the log below.
	for connection in asc.attribute_changed.get_connections():
		var watcher = connection["callable"].get_object()
		if watcher is AbilityAsync:
			watcher.end_action()

	print("=== tags ===")
	log.clear()
	asc.add_loose_gameplay_tag(_tag(&"Status.Stunned"))
	var added := AbilityAsync.wait_gameplay_tag_added(asc, &"Status")
	added.triggered.connect(func(tag: StringName) -> void: log.append(["added", tag]))
	_check("a tag already there is not reported before the caller could connect", "" if log.is_empty() else str(log))
	await _frames(1)
	_check("but is reported - a child tag counting for its parent", "" if log == [["added", &"Status"]] else str(log))
	asc.add_loose_gameplay_tag(_tag(&"Status.Slowed"))
	_check("a second matching tag is not a new arrival", "" if log.size() == 1 else str(log))
	asc.remove_loose_gameplay_tag(_tag(&"Status.Stunned"))
	asc.remove_loose_gameplay_tag(_tag(&"Status.Slowed"))
	asc.add_loose_gameplay_tag(_tag(&"Status.Stunned"))
	_check("gone and back is", "" if log.size() == 2 else str(log))
	added.end_action()

	log.clear()
	AbilityAsync.wait_gameplay_tag_removed(asc, &"State.Missing").triggered.connect(func(tag: StringName) -> void: log.append(tag))
	await _frames(1)
	_check("wait_gameplay_tag_removed on a tag not there reports at once", "" if log == [&"State.Missing"] else str(log))

	print("=== once, awaited ===")
	log.clear()
	var once := AbilityAsync.wait_gameplay_tag_removed(asc, &"Status.Stunned", null, true)
	var once_ref: WeakRef = weakref(once)
	_await_and_log(once, "stun over")
	await _frames(1)
	_check("an awaited async waits", "" if log.is_empty() else str(log))
	asc.remove_loose_gameplay_tag(_tag(&"Status.Stunned"))
	_check("and resumes the coroutine when it triggers", "" if log == [["stun over", &"Status.Stunned"]] else str(log))
	_check("then it has ended", "" if once.has_ended() and not once.is_active() else "still active")
	once = null
	await _frames(1)
	_check("and the target lets go of it", "" if once_ref.get_ref() == null else "still alive")

	print("=== events and effects ===")
	log.clear()
	var events := AbilityAsync.wait_gameplay_event(asc, &"Event.Test")
	events.triggered.connect(func(payload: Dictionary) -> void: log.append(payload.get("damage")))
	asc.send_gameplay_event(&"Event.Other", { "damage": 1 })
	asc.send_gameplay_event(&"Event.Test", { "damage": 12 })
	_check("wait_gameplay_event hears its event only, with the payload", "" if log == [12] else str(log))
	events.end_action()

	log.clear()
	var effects := AbilityAsync.wait_gameplay_effect_applied(asc, &"Effect.Buff")
	effects.triggered.connect(func(applied: Dictionary) -> void: log.append(applied["active_id"]))
	asc.apply_gameplay_effect_to_self(_effect(["Effect.Damage"]))
	var buff_id := asc.apply_gameplay_effect_to_self(_effect(["Effect.Buff.Haste"]))
	_check("wait_gameplay_effect_applied filters by effect tag, children included",
		"" if log == [buff_id] else "%s, wanted [%d]" % [str(log), buff_id])
	effects.end_action()

	print("=== ending ===")
	log.clear()
	var ended := AbilityAsync.wait_attribute_change(asc, &"health")
	var ended_ref: WeakRef = weakref(ended)
	ended.triggered.connect(func(_change: Dictionary) -> void: log.append("heard"))
	_await_and_log(ended, "after await")
	ended.end_action()
	asc.set_attribute_base_value(&"health", 50.0)
	_check("end_action: no more triggers, and the code after an await on it never runs", "" if log.is_empty() else str(log))
	ended = null
	await _frames(1)
	_check("and it is freed, not leaked", "" if ended_ref.get_ref() == null else "still alive")

	log.clear()
	var widget := Node.new()
	root.add_child(widget)
	var owned := AbilityAsync.wait_attribute_change(asc, &"health", widget)
	owned.triggered.connect(func(_change: Dictionary) -> void: log.append("widget heard"))
	asc.set_attribute_base_value(&"health", 40.0)
	widget.queue_free()
	await _frames(1)
	asc.set_attribute_base_value(&"health", 30.0)
	_check("an owner leaving the tree ends it", "" if log == ["widget heard"] and owned.has_ended() else "%s ended=%s" % [str(log), owned.has_ended()])

	log.clear()
	var loose := Node.new()
	var orphaned := AbilityAsync.wait_attribute_change(asc, &"health", loose)
	orphaned.triggered.connect(func(_change: Dictionary) -> void: log.append("orphan heard"))
	loose.free()
	asc.set_attribute_base_value(&"health", 20.0)
	_check("an owner freed without ever being in the tree ends it too", "" if log.is_empty() and orphaned.has_ended() else str(log))

	log.clear()
	var doomed := _make_asc()
	var watching := AbilityAsync.wait_gameplay_event(doomed, &"Event.Test")
	var watching_ref: WeakRef = weakref(watching)
	_await_and_log(watching, "doomed")
	watching = null
	doomed.free()
	await _frames(1)
	_check("a target freed ends what watches it: the await never resumes", "" if log.is_empty() else str(log))
	_check("and the async is freed with it", "" if watching_ref.get_ref() == null else "still alive")

	print("=== a GDScript async ===")
	log.clear()
	var ability := GameplayAbility.new()
	ability.ability_name = &"Poke"
	asc.give_ability(ability)
	var custom := WaitActivation.new().activate(asc, null, true)
	custom.triggered.connect(func(ability_name: StringName) -> void: log.append(ability_name))
	asc.try_activate_ability(&"Poke")
	_check("a subclass hooks in _activate and triggers with broadcast", "" if log == [&"Poke"] else str(log))
	asc.find_ability_by_name(&"Poke").end_ability(false)
	asc.try_activate_ability(&"Poke")
	_check("once, it unhooks through _on_end", "" if log == [&"Poke"] and not asc.ability_activated.get_connections().any(
		func(connection: Dictionary) -> bool: return connection["callable"].get_object() == custom) else str(log))

	if failures.is_empty():
		print("test_ability_async: all checks passed")
		quit(0)
	else:
		print("test_ability_async: %d check(s) failed" % failures.size())
		quit(1)
