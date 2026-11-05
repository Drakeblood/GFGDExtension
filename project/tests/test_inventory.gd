extends SceneTree

# Inventory, quick bar and equipment on one machine: stacking and capacity,
# fragments, the quick bar putting the active slot's item on, and equipment
# granting - and taking back - abilities, effects, tags and spawned scenes.
#
# Replication is checked by a game's own host + client tests; everything here
# runs with no network, where every component is its own authority.
#
# Run with:
#   godot --headless --path project --script res://tests/test_inventory.gd

var failures: Array[String] = []
var holder: Node3D
var inventory: InventoryManagerComponent
var quick_bar: QuickBarComponent
var equipment: EquipmentManagerComponent
var asc: AbilitySystemComponent
var skeleton: Skeleton3D


# A game's own fragment: counts how many instances it saw created.
class CountingFragment extends InventoryItemFragment:
	var created := 0

	func _on_instance_created(instance: InventoryItemInstance) -> void:
		created += 1
		instance.set_data(&"serial", created)


# A game's own equipment instance.
class LampInstance extends EquipmentInstance:
	static var equipped_count := 0
	static var unequipped_count := 0

	func _on_equipped() -> void:
		equipped_count += 1

	func _on_unequipped() -> void:
		unequipped_count += 1


func _check(name: String, complaint: String) -> void:
	print("    %s %s%s" % ["PASS" if complaint.is_empty() else "FAIL", name,
		"" if complaint.is_empty() else "  -  " + complaint])
	if not complaint.is_empty():
		failures.append("%s: %s" % [name, complaint])


func _expect(name: String, actual: Variant, expected: Variant) -> void:
	_check(name, "" if actual == expected else "got %s, expected %s" % [actual, expected])


func _definition(display_name: String, max_stack: int, fragments: Array) -> InventoryItemDefinition:
	var definition := InventoryItemDefinition.new()
	definition.display_name = display_name
	definition.max_stack_size = max_stack
	var typed: Array[InventoryItemFragment] = []
	for fragment in fragments:
		typed.append(fragment)
	definition.fragments = typed
	return definition


func _tag(name: StringName) -> GameplayTag:
	return GameplayTagsManager.get_singleton().request_tag(name)


func _initialize() -> void:
	holder = Node3D.new()
	holder.name = "Holder"
	root.add_child(holder)
	asc = AbilitySystemComponent.new()
	var attributes := AttributeSet.new()
	attributes.define_attribute(&"speed", 10.0)
	asc.attribute_set = attributes
	holder.add_child(asc)
	inventory = InventoryManagerComponent.new()
	holder.add_child(inventory)
	quick_bar = QuickBarComponent.new()
	quick_bar.num_slots = 5
	holder.add_child(quick_bar)
	equipment = EquipmentManagerComponent.new()
	holder.add_child(equipment)

	skeleton = Skeleton3D.new()
	skeleton.add_bone("hand.r")
	holder.add_child(skeleton)
	_run.call_deferred()


func _run() -> void:
	await physics_frame

	print("=== stacks and capacity ===")
	var counting := CountingFragment.new()
	var stats := InventoryFragmentSetStats.new()
	stats.initial_stats = {&"Inventory.Charges": 3}
	var plank := _definition("Plank", 10, [counting, stats])
	var first := inventory.add_item_definition(plank, 25)
	_expect("a new entry is capped at the max stack", inventory.get_stack_count(first), 10)
	_expect("add_items tops up and starts new stacks", inventory.add_items(plank, 15), 15)
	_expect("units, not entries", inventory.get_total_item_count_by_definition(plank), 25)
	_expect("entries", inventory.get_entry_count(), 3)
	_expect("SetStats gave every instance its charges", first.get_stat_tag_stack_count(&"Inventory.Charges"), 3)
	_expect("a script fragment ran for every new instance", counting.created, 3)
	_expect("and wrote per-instance data", first.get_data(&"serial"), 1)
	_check("ids are unique", "" if first.get_item_id() != (inventory.get_all_items()[1] as InventoryItemInstance).get_item_id() else "two instances share an id")

	_check("consume refuses more than there is", "" if not inventory.consume_items(plank, 26) else "took 26 of 25")
	_expect("and takes nothing", inventory.get_total_item_count_by_definition(plank), 25)
	_check("consume takes what there is", "" if inventory.consume_items(plank, 12) else "refused 12 of 25")
	_expect("newest stacks go first", inventory.get_total_item_count_by_definition(plank), 13)
	_expect("an emptied stack leaves", inventory.get_entry_count(), 2)

	inventory.clear()
	inventory.max_entries = 2
	_check("room for two stacks", "" if inventory.can_add_item_definition(plank, 20) else "20 should fit in 2 x 10")
	_check("but not three", "" if not inventory.can_add_item_definition(plank, 21) else "21 should not fit")
	_expect("add_items stops when full", inventory.add_items(plank, 30), 20)
	_check("add_item_definition refuses when full", "" if inventory.add_item_definition(plank) == null else "made a third entry")
	inventory.clear()
	inventory.max_entries = 0

	print("=== fragments ===")
	_check("by native class name", "" if plank.find_fragment_by_class(&"InventoryFragmentSetStats") == stats else "not found")
	_check("by script", "" if plank.find_fragment_by_class(CountingFragment) == counting else "not found")
	_check("by base class", "" if plank.find_fragment_by_class(&"InventoryItemFragment") == counting else "not the first fragment")
	_check("absent", "" if plank.find_fragment_by_class(&"InventoryFragmentPickup") == null else "found something")

	print("=== quick bar and equipment ===")
	var ability := GameplayAbility.new()
	ability.ability_name = &"chop"
	var effect := GameplayEffect.new()
	effect.duration_policy = GameplayEffect.INFINITE
	var modifier := AttributeModifier.new()
	modifier.attribute = &"speed"
	modifier.operation = AttributeModifier.ADD
	modifier.magnitude = -2.0
	var modifiers: Array[AttributeModifier] = [modifier]
	effect.modifiers = modifiers
	var spawn := EquipmentActorToSpawn.new()
	var axe_scene := PackedScene.new()
	var axe_node := MeshInstance3D.new()
	axe_node.name = "AxeMesh"
	axe_scene.pack(axe_node)
	axe_node.free()
	spawn.scene = axe_scene
	spawn.attach_bone = &"hand.r"
	spawn.attach_transform = Transform3D(Basis.IDENTITY, Vector3(0, 0.1, 0))
	var axe_equipment := EquipmentDefinition.new()
	axe_equipment.abilities_to_grant = [ability]
	axe_equipment.effects_to_apply = [effect]
	axe_equipment.granted_tags = PackedStringArray(["State.Equipped.Axe"])
	var spawns: Array[EquipmentActorToSpawn] = [spawn]
	axe_equipment.actors_to_spawn = spawns
	var equippable := InventoryFragmentEquippableItem.new()
	equippable.equipment_definition = axe_equipment
	var axe := _definition("Axe", 1, [equippable, InventoryFragmentQuickBarIcon.new()])

	var lamp_equipment := EquipmentDefinition.new()
	lamp_equipment.instance_script = LampInstance
	var lamp_equippable := InventoryFragmentEquippableItem.new()
	lamp_equippable.equipment_definition = lamp_equipment
	var lamp := _definition("Lamp", 1, [lamp_equippable])
	var rock := _definition("Rock", 5, [])

	var axe_item := inventory.add_item_definition(axe)
	var rock_item := inventory.add_item_definition(rock, 3)
	var lamp_item := inventory.add_item_definition(lamp)
	_expect("equippable items took slots by themselves", quick_bar.find_slot_of_item(axe_item), 0)
	_expect("in order", quick_bar.find_slot_of_item(lamp_item), 1)
	_expect("an item with no icon and no equipment did not", quick_bar.find_slot_of_item(rock_item), -1)
	_expect("nothing is active yet", quick_bar.get_active_slot_index(), -1)
	_expect("nothing is on yet", equipment.get_equipment_instances().size(), 0)

	quick_bar.set_active_slot_index(0)
	var on := quick_bar.get_equipped_item()
	_check("the active slot's item is on", "" if on != null and on.get_item_id() == axe_item.get_item_id() else "nothing equipped")
	_check("with its instigator", "" if on != null and on.get_instigator() == axe_item else "wrong instigator")
	_check("the ability is granted", "" if asc.find_ability_by_name(&"chop") != null else "not granted")
	_check("the tag is held", "" if asc.has_matching_gameplay_tag(_tag(&"State.Equipped.Axe")) else "no tag")
	_expect("the effect applies", asc.get_attribute_value(&"speed"), 8.0)
	var spawned := on.get_spawned_nodes() if on != null else []
	var mesh: Node3D = null
	for node in spawned:
		if node is MeshInstance3D:
			mesh = node
	_check("the scene follows the bone", "" if mesh != null and mesh.get_parent() is BoneAttachment3D and (mesh.get_parent() as BoneAttachment3D).bone_name == &"hand.r" else "not attached to hand.r")
	_check("at the attach transform", "" if mesh != null and mesh.position.is_equal_approx(Vector3(0, 0.1, 0)) else "wrong offset")
	on.set_spawned_visible(false)
	_check("hidden on request", "" if mesh != null and not mesh.visible else "still visible")

	quick_bar.cycle_active_slot_forward()
	_expect("cycling moves to the next filled slot", quick_bar.get_active_slot_index(), 1)
	_check("the axe's ability went with it", "" if asc.find_ability_by_name(&"chop") == null else "still granted")
	_check("so did the tag", "" if not asc.has_matching_gameplay_tag(_tag(&"State.Equipped.Axe")) else "tag left behind")
	_expect("and the effect", asc.get_attribute_value(&"speed"), 10.0)
	_expect("a script instance ran _on_equipped", LampInstance.equipped_count, 1)
	_check("get_first_instance_of_type finds it", "" if equipment.get_first_instance_of_type(LampInstance) != null else "not found")
	quick_bar.cycle_active_slot_forward()
	_expect("cycling wraps around", quick_bar.get_active_slot_index(), 0)
	_expect("and ran _on_unequipped", LampInstance.unequipped_count, 1)
	await process_frame
	_check("the unequipped scene is gone", "" if not is_instance_valid(mesh) else "still in the tree")

	quick_bar.swap_slots(0, 1)
	_expect("a swap moves the item", quick_bar.find_slot_of_item(axe_item), 1)
	_check("the active slot now holds the lamp", "" if quick_bar.get_equipped_item() != null and quick_bar.get_equipped_item().get_item_id() == lamp_item.get_item_id() else "wrong item on")

	inventory.remove_item_instance(lamp_item)
	_expect("removing an item empties its slot", quick_bar.find_slot_of_item(lamp_item), -1)
	_expect("and deselects it", quick_bar.get_active_slot_index(), -1)
	_expect("and takes it off", equipment.get_equipment_instances().size(), 0)

	quick_bar.set_active_slot_index(1)
	quick_bar.set_active_slot_index(-1)
	_expect("-1 puts nothing on", equipment.get_equipment_instances().size(), 0)
	quick_bar.allow_no_active_slot = false
	quick_bar.set_active_slot_index(1)
	quick_bar.set_active_slot_index(-1)
	_expect("unless allow_no_active_slot is off", quick_bar.get_active_slot_index(), 1)

	print("=== moving between inventories ===")
	var chest := InventoryManagerComponent.new()
	root.add_child(chest)
	axe_item.set_stat_tag_stack(&"Inventory.Durability", 7)
	chest.add_item_instance(axe_item)
	_check("the instance left the backpack", "" if inventory.find_item_by_id(axe_item.get_item_id()) == null else "still there")
	_check("and is in the chest", "" if chest.find_item_by_id(axe_item.get_item_id()) == axe_item else "not in the chest")
	_expect("with its state", axe_item.get_stat_tag_stack_count(&"Inventory.Durability"), 7)
	_expect("it left the quick bar too", quick_bar.find_slot_of_item(axe_item), -1)
	_check("and the pawn", "" if asc.find_ability_by_name(&"chop") == null else "still granted")
	_check("find_item_anywhere sees it", "" if InventoryManagerComponent.find_item_anywhere(holder, axe_item.get_item_id()) == axe_item else "not found")

	print("")
	if failures.is_empty():
		print("ALL PASSED")
		quit(0)
	else:
		print("%d FAILED" % failures.size())
		for failure in failures:
			print("  " + failure)
		quit(1)
