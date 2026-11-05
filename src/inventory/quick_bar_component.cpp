#include "inventory/quick_bar_component.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "framework/controller.h"
#include "framework/world.h"
#include "inventory/component_replication.h"
#include "inventory/equipment_manager_component.h"
#include "inventory/inventory_fragments.h"
#include "inventory/inventory_manager_component.h"

namespace GFGD
{
namespace
{
ComponentReplication::Audience audience_of(bool replicated)
{
	return replicated ? ComponentReplication::AUDIENCE_OWNER : ComponentReplication::AUDIENCE_NONE;
}

EquipmentManagerComponent* find_manager_under(Node* node)
{
	if (node == nullptr) { return nullptr; }
	const int count = node->get_child_count();
	for (int i = 0; i < count; i++)
	{
		if (EquipmentManagerComponent* manager = Object::cast_to<EquipmentManagerComponent>(node->get_child(i))) { return manager; }
	}
	return nullptr;
}
}

void QuickBarComponent::_ready()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	ensure_slot_count();

	PackedStringArray from_anyone;
	from_anyone.push_back("server_request_full_state");
	from_anyone.push_back("server_set_active_slot_index");
	from_anyone.push_back("server_swap_slots");
	PackedStringArray from_server;
	from_server.push_back("client_receive_state");
	ComponentReplication::configure_rpcs(this, from_anyone, from_server);

	connect_inventory();

	if (Controller* controller = Object::cast_to<Controller>(get_parent()))
	{
		controller->connect("pawn_changed", callable_mp(this, &QuickBarComponent::_on_pawn_changed));
	}

	if (is_mirror())
	{
		rpc_id(World::SERVER_PEER_ID, "server_request_full_state");
	}
}

bool QuickBarComponent::is_mirror() const
{
	return ComponentReplication::is_mirror(this, audience_of(replicated));
}

bool QuickBarComponent::refuse_on_mirror(const char* what) const
{
	if (!is_mirror()) { return false; }
	ERR_PRINT(vformat("GFGD: %s called on a client's mirror of %s; the quick bar's slots change on the server.", String(what), String(get_path())));
	return true;
}

void QuickBarComponent::ensure_slot_count()
{
	while (slot_ids.size() < num_slots) { slot_ids.push_back(0); }
	if (slot_ids.size() > num_slots) { slot_ids.resize(num_slots); }
	if (active_slot_index >= num_slots) { active_slot_index = -1; }
}

void QuickBarComponent::set_num_slots(int value)
{
	num_slots = MAX(value, 1);
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint())
	{
		ensure_slot_count();
		queue_flush();
		emit_signal("slots_changed");
	}
}

// --- Neighbours ---

InventoryManagerComponent* QuickBarComponent::get_inventory() const
{
	if (!inventory_path.is_empty())
	{
		return Object::cast_to<InventoryManagerComponent>(get_node_or_null(inventory_path));
	}
	Node* parent = get_parent();
	if (parent == nullptr) { return nullptr; }
	const int count = parent->get_child_count();
	for (int i = 0; i < count; i++)
	{
		if (InventoryManagerComponent* inventory = Object::cast_to<InventoryManagerComponent>(parent->get_child(i))) { return inventory; }
	}
	return nullptr;
}

EquipmentManagerComponent* QuickBarComponent::get_equipment_manager() const
{
	Node* parent = get_parent();
	if (Controller* controller = Object::cast_to<Controller>(parent))
	{
		return find_manager_under(controller->get_pawn_root());
	}
	return find_manager_under(parent);
}

void QuickBarComponent::connect_inventory()
{
	InventoryManagerComponent* inventory = get_inventory();
	if (inventory == nullptr)
	{
		WARN_PRINT(vformat("GFGD: QuickBarComponent %s found no InventoryManagerComponent beside it.", String(get_path())));
		return;
	}
	inventory->connect("item_added", callable_mp(this, &QuickBarComponent::_on_inventory_item_added));
	inventory->connect("item_removed", callable_mp(this, &QuickBarComponent::_on_inventory_item_removed));
	inventory->connect("changed", callable_mp(this, &QuickBarComponent::_on_inventory_changed));
}

void QuickBarComponent::_on_inventory_item_added(const Ref<InventoryItemInstance>& item, int)
{
	if (is_mirror() || !auto_slot_new_items || item.is_null() || find_slot_of_item(item) >= 0) { return; }
	const bool slottable = item->find_fragment_by_class(StringName("InventoryFragmentQuickBarIcon")).is_valid()
		|| item->find_fragment_by_class(StringName("InventoryFragmentEquippableItem")).is_valid();
	if (!slottable) { return; }
	const int slot = get_next_free_item_slot();
	if (slot >= 0) { add_item_to_slot(slot, item); }
}

void QuickBarComponent::_on_inventory_item_removed(const Ref<InventoryItemInstance>& item)
{
	if (is_mirror()) { return; }
	const int slot = find_slot_of_item(item);
	if (slot >= 0) { remove_item_from_slot(slot); }
}

void QuickBarComponent::_on_inventory_changed()
{
	// A mirror's slots name items by id; one that arrives after the slot list
	// resolves now, so the UI redraws.
	if (is_mirror()) { emit_signal("slots_changed"); }
}

void QuickBarComponent::_on_pawn_changed(Object*)
{
	if (is_mirror()) { return; }
	// The old pawn's manager takes its equipment down itself when it leaves;
	// a pawn that only lost its controller gets it taken off here.
	unequip_item_in_slot();
	if (active_slot_index >= 0) { equip_item_in_slot(); }
}

// --- Equipping (server) ---

void QuickBarComponent::equip_item_in_slot()
{
	if (equipped.is_valid()) { return; }
	Ref<InventoryItemInstance> item = get_active_slot_item();
	if (item.is_null()) { return; }
	Ref<InventoryFragmentEquippableItem> equippable = item->find_fragment_by_class(StringName("InventoryFragmentEquippableItem"));
	if (equippable.is_null() || equippable->get_equipment_definition().is_null()) { return; }
	EquipmentManagerComponent* manager = get_equipment_manager();
	if (manager == nullptr) { return; }
	equipped = manager->equip_item(equippable->get_equipment_definition(), item);
	equipped_manager_id = ObjectID(manager->get_instance_id());
}

void QuickBarComponent::unequip_item_in_slot()
{
	if (equipped.is_null()) { return; }
	EquipmentManagerComponent* manager = Object::cast_to<EquipmentManagerComponent>(ObjectDB::get_instance(equipped_manager_id));
	if (manager != nullptr && manager->is_inside_tree()) { manager->unequip_item(equipped); }
	equipped.unref();
	equipped_manager_id = ObjectID();
}

// --- Active slot ---

void QuickBarComponent::set_active_slot_index(int index)
{
	if (is_mirror())
	{
		rpc_id(World::SERVER_PEER_ID, "server_set_active_slot_index", index);
		return;
	}
	apply_active_slot_index(index);
}

void QuickBarComponent::apply_active_slot_index(int index)
{
	if (index < 0)
	{
		if (!allow_no_active_slot || active_slot_index < 0) { return; }
		unequip_item_in_slot();
		active_slot_index = -1;
	}
	else
	{
		if (index >= slot_ids.size() || index == active_slot_index) { return; }
		unequip_item_in_slot();
		active_slot_index = index;
		equip_item_in_slot();
	}
	queue_flush();
	emit_signal("active_slot_changed", active_slot_index);
}

void QuickBarComponent::cycle_active_slot_forward()
{
	const int size = slot_ids.size();
	if (size < 2) { return; }
	const int start = active_slot_index < 0 ? size - 1 : active_slot_index;
	int index = start;
	do
	{
		index = (index + 1) % size;
		if (slot_ids[index] != 0)
		{
			set_active_slot_index(index);
			return;
		}
	} while (index != start);
}

void QuickBarComponent::cycle_active_slot_backward()
{
	const int size = slot_ids.size();
	if (size < 2) { return; }
	const int start = active_slot_index < 0 ? 0 : active_slot_index;
	int index = start;
	do
	{
		index = (index - 1 + size) % size;
		if (slot_ids[index] != 0)
		{
			set_active_slot_index(index);
			return;
		}
	} while (index != start);
}

void QuickBarComponent::swap_slots(int from, int to)
{
	if (is_mirror())
	{
		rpc_id(World::SERVER_PEER_ID, "server_swap_slots", from, to);
		return;
	}
	apply_swap(from, to);
}

void QuickBarComponent::apply_swap(int from, int to)
{
	if (from == to || from < 0 || to < 0 || from >= slot_ids.size() || to >= slot_ids.size()) { return; }
	// The active index stays on its slot, so what is in the hand changes when
	// the active slot takes part.
	const bool touches_active = from == active_slot_index || to == active_slot_index;
	if (touches_active) { unequip_item_in_slot(); }
	const int64_t moved = slot_ids[from];
	slot_ids.set(from, slot_ids[to]);
	slot_ids.set(to, moved);
	if (touches_active) { equip_item_in_slot(); }
	queue_flush();
	emit_signal("slots_changed");
}

// --- Slots ---

Ref<InventoryItemInstance> QuickBarComponent::get_slot_item(int index) const
{
	if (index < 0 || index >= slot_ids.size() || slot_ids[index] == 0) { return Ref<InventoryItemInstance>(); }
	InventoryManagerComponent* inventory = get_inventory();
	return inventory != nullptr ? inventory->find_item_by_id(slot_ids[index]) : Ref<InventoryItemInstance>();
}

Array QuickBarComponent::get_slots() const
{
	Array slots;
	for (int i = 0; i < slot_ids.size(); i++)
	{
		Ref<InventoryItemInstance> item = get_slot_item(i);
		slots.push_back(item.is_valid() ? Variant(item) : Variant());
	}
	return slots;
}

Ref<InventoryItemInstance> QuickBarComponent::get_active_slot_item() const
{
	return get_slot_item(active_slot_index);
}

bool QuickBarComponent::is_item_slot_free(int index) const
{
	return index >= 0 && index < slot_ids.size() && slot_ids[index] == 0;
}

int QuickBarComponent::get_next_free_item_slot() const
{
	for (int i = 0; i < slot_ids.size(); i++)
	{
		if (slot_ids[i] == 0) { return i; }
	}
	return -1;
}

int QuickBarComponent::find_slot_of_item(const Ref<InventoryItemInstance>& item) const
{
	if (item.is_null()) { return -1; }
	for (int i = 0; i < slot_ids.size(); i++)
	{
		if (slot_ids[i] == item->get_item_id()) { return i; }
	}
	return -1;
}

bool QuickBarComponent::add_item_to_slot(int index, const Ref<InventoryItemInstance>& item)
{
	if (refuse_on_mirror("add_item_to_slot")) { return false; }
	if (item.is_null() || !is_item_slot_free(index)) { return false; }
	slot_ids.set(index, item->get_item_id());
	if (index == active_slot_index) { equip_item_in_slot(); }
	queue_flush();
	emit_signal("slots_changed");
	return true;
}

Ref<InventoryItemInstance> QuickBarComponent::remove_item_from_slot(int index)
{
	if (refuse_on_mirror("remove_item_from_slot")) { return Ref<InventoryItemInstance>(); }
	if (index < 0 || index >= slot_ids.size()) { return Ref<InventoryItemInstance>(); }
	Ref<InventoryItemInstance> item = get_slot_item(index);
	if (index == active_slot_index)
	{
		unequip_item_in_slot();
		active_slot_index = -1;
		emit_signal("active_slot_changed", active_slot_index);
	}
	if (slot_ids[index] != 0)
	{
		slot_ids.set(index, 0);
		emit_signal("slots_changed");
	}
	queue_flush();
	return item;
}

void QuickBarComponent::reset_slots()
{
	if (refuse_on_mirror("reset_slots")) { return; }
	unequip_item_in_slot();
	for (int i = 0; i < slot_ids.size(); i++) { slot_ids.set(i, 0); }
	active_slot_index = -1;
	queue_flush();
	emit_signal("slots_changed");
	emit_signal("active_slot_changed", active_slot_index);
}

Ref<EquipmentInstance> QuickBarComponent::get_equipped_item() const
{
	if (equipped.is_valid()) { return equipped; }
	EquipmentManagerComponent* manager = get_equipment_manager();
	if (manager == nullptr || active_slot_index < 0 || active_slot_index >= slot_ids.size()) { return Ref<EquipmentInstance>(); }
	return manager->find_instance_by_item_id(slot_ids[active_slot_index]);
}

// --- Replication ---

void QuickBarComponent::queue_flush()
{
	if (flush_queued || !ComponentReplication::is_sending_server(this, audience_of(replicated))) { return; }
	flush_queued = true;
	callable_mp(this, &QuickBarComponent::flush_replication).call_deferred();
}

void QuickBarComponent::flush_replication()
{
	flush_queued = false;
	if (!is_inside_tree() || !ComponentReplication::is_sending_server(this, audience_of(replicated))) { return; }
	const PackedInt32Array peers = ComponentReplication::resolve_peers(this, audience_of(replicated), subscribers);
	for (int i = 0; i < peers.size(); i++)
	{
		rpc_id(peers[i], "client_receive_state", slot_ids, active_slot_index);
	}
}

void QuickBarComponent::server_request_full_state()
{
	if (!ComponentReplication::is_sending_server(this, audience_of(replicated))) { return; }
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	const int sender = multiplayer.is_valid() ? multiplayer->get_remote_sender_id() : 0;
	if (sender <= 0 || sender != ComponentReplication::get_owner_peer(this)) { return; }
	subscribers.insert(sender);
	rpc_id(sender, "client_receive_state", slot_ids, active_slot_index);
}

void QuickBarComponent::server_set_active_slot_index(int index)
{
	if (!ComponentReplication::is_sending_server(this, audience_of(replicated)) || !ComponentReplication::sender_is_owner(this, "a quick bar slot change")) { return; }
	apply_active_slot_index(index);
}

void QuickBarComponent::server_swap_slots(int from, int to)
{
	if (!ComponentReplication::is_sending_server(this, audience_of(replicated)) || !ComponentReplication::sender_is_owner(this, "a quick bar swap")) { return; }
	apply_swap(from, to);
}

void QuickBarComponent::client_receive_state(const PackedInt64Array& slots, int active_index)
{
	const bool slots_differ = slots != slot_ids;
	const bool active_differs = active_index != active_slot_index;
	slot_ids = slots;
	num_slots = slot_ids.size();
	active_slot_index = active_index;
	if (slots_differ) { emit_signal("slots_changed"); }
	if (active_differs) { emit_signal("active_slot_changed", active_slot_index); }
}

void QuickBarComponent::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("is_mirror"), &QuickBarComponent::is_mirror);
	ClassDB::bind_method(D_METHOD("get_inventory"), &QuickBarComponent::get_inventory);
	ClassDB::bind_method(D_METHOD("get_equipment_manager"), &QuickBarComponent::get_equipment_manager);
	ClassDB::bind_method(D_METHOD("set_active_slot_index", "index"), &QuickBarComponent::set_active_slot_index);
	ClassDB::bind_method(D_METHOD("get_active_slot_index"), &QuickBarComponent::get_active_slot_index);
	ClassDB::bind_method(D_METHOD("cycle_active_slot_forward"), &QuickBarComponent::cycle_active_slot_forward);
	ClassDB::bind_method(D_METHOD("cycle_active_slot_backward"), &QuickBarComponent::cycle_active_slot_backward);
	ClassDB::bind_method(D_METHOD("swap_slots", "from", "to"), &QuickBarComponent::swap_slots);
	ClassDB::bind_method(D_METHOD("get_slots"), &QuickBarComponent::get_slots);
	ClassDB::bind_method(D_METHOD("get_slot_item", "index"), &QuickBarComponent::get_slot_item);
	ClassDB::bind_method(D_METHOD("get_active_slot_item"), &QuickBarComponent::get_active_slot_item);
	ClassDB::bind_method(D_METHOD("is_item_slot_free", "index"), &QuickBarComponent::is_item_slot_free);
	ClassDB::bind_method(D_METHOD("get_next_free_item_slot"), &QuickBarComponent::get_next_free_item_slot);
	ClassDB::bind_method(D_METHOD("find_slot_of_item", "item"), &QuickBarComponent::find_slot_of_item);
	ClassDB::bind_method(D_METHOD("add_item_to_slot", "index", "item"), &QuickBarComponent::add_item_to_slot);
	ClassDB::bind_method(D_METHOD("remove_item_from_slot", "index"), &QuickBarComponent::remove_item_from_slot);
	ClassDB::bind_method(D_METHOD("reset_slots"), &QuickBarComponent::reset_slots);
	ClassDB::bind_method(D_METHOD("get_equipped_item"), &QuickBarComponent::get_equipped_item);

	ClassDB::bind_method(D_METHOD("set_num_slots", "value"), &QuickBarComponent::set_num_slots);
	ClassDB::bind_method(D_METHOD("get_num_slots"), &QuickBarComponent::get_num_slots);
	ClassDB::bind_method(D_METHOD("set_allow_no_active_slot", "value"), &QuickBarComponent::set_allow_no_active_slot);
	ClassDB::bind_method(D_METHOD("get_allow_no_active_slot"), &QuickBarComponent::get_allow_no_active_slot);
	ClassDB::bind_method(D_METHOD("set_auto_slot_new_items", "value"), &QuickBarComponent::set_auto_slot_new_items);
	ClassDB::bind_method(D_METHOD("get_auto_slot_new_items"), &QuickBarComponent::get_auto_slot_new_items);
	ClassDB::bind_method(D_METHOD("set_replicated", "value"), &QuickBarComponent::set_replicated);
	ClassDB::bind_method(D_METHOD("get_replicated"), &QuickBarComponent::get_replicated);
	ClassDB::bind_method(D_METHOD("set_inventory_path", "value"), &QuickBarComponent::set_inventory_path);
	ClassDB::bind_method(D_METHOD("get_inventory_path"), &QuickBarComponent::get_inventory_path);

	ClassDB::bind_method(D_METHOD("server_request_full_state"), &QuickBarComponent::server_request_full_state);
	ClassDB::bind_method(D_METHOD("server_set_active_slot_index", "index"), &QuickBarComponent::server_set_active_slot_index);
	ClassDB::bind_method(D_METHOD("server_swap_slots", "from", "to"), &QuickBarComponent::server_swap_slots);
	ClassDB::bind_method(D_METHOD("client_receive_state", "slots", "active_index"), &QuickBarComponent::client_receive_state);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "num_slots", PROPERTY_HINT_RANGE, "1,20,1,or_greater"), "set_num_slots", "get_num_slots");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "allow_no_active_slot"), "set_allow_no_active_slot", "get_allow_no_active_slot");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_slot_new_items"), "set_auto_slot_new_items", "get_auto_slot_new_items");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "replicated"), "set_replicated", "get_replicated");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "inventory_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "InventoryManagerComponent"), "set_inventory_path", "get_inventory_path");

	ADD_SIGNAL(MethodInfo("slots_changed"));
	ADD_SIGNAL(MethodInfo("active_slot_changed", PropertyInfo(Variant::INT, "index")));
}
}
