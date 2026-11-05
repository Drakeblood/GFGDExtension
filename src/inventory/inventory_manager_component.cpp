#include "inventory/inventory_manager_component.h"

#include <climits>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "framework/world.h"
#include "inventory/component_replication.h"

namespace GFGD
{
int64_t InventoryManagerComponent::next_item_id = 1;

namespace
{
ComponentReplication::Audience audience_of(InventoryManagerComponent::ReplicationMode mode)
{
	switch (mode)
	{
		case InventoryManagerComponent::REPLICATION_OWNER: return ComponentReplication::AUDIENCE_OWNER;
		case InventoryManagerComponent::REPLICATION_EVERYONE: return ComponentReplication::AUDIENCE_EVERYONE;
		default: return ComponentReplication::AUDIENCE_NONE;
	}
}
}

void InventoryManagerComponent::_ready()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	add_to_group(INVENTORIES_GROUP);

	PackedStringArray from_anyone;
	from_anyone.push_back("server_request_full_state");
	PackedStringArray from_server;
	from_server.push_back("client_receive_full_state");
	from_server.push_back("client_update");
	ComponentReplication::configure_rpcs(this, from_anyone, from_server);

	if (is_mirror())
	{
		rpc_id(World::SERVER_PEER_ID, "server_request_full_state");
	}
}

bool InventoryManagerComponent::is_mirror() const
{
	return ComponentReplication::is_mirror(this, audience_of(replication_mode));
}

bool InventoryManagerComponent::refuse_on_mirror(const char* what) const
{
	if (!is_mirror()) { return false; }
	ERR_PRINT(vformat("GFGD: %s called on a client's mirror of %s; inventories change on the server.", String(what), String(get_path())));
	return true;
}

// --- Lookups ---

int InventoryManagerComponent::find_entry_index(const Ref<InventoryItemInstance>& instance) const
{
	if (instance.is_null()) { return -1; }
	for (int i = 0; i < entries.size(); i++)
	{
		if (entries[i].instance == instance) { return i; }
	}
	return -1;
}

int InventoryManagerComponent::find_entry_index_by_id(int64_t item_id) const
{
	for (int i = 0; i < entries.size(); i++)
	{
		if (entries[i].instance->get_item_id() == item_id) { return i; }
	}
	return -1;
}

int InventoryManagerComponent::free_entry_slots() const
{
	return max_entries <= 0 ? INT_MAX : MAX(max_entries - entries.size(), 0);
}

Array InventoryManagerComponent::get_all_items() const
{
	Array items;
	for (int i = 0; i < entries.size(); i++) { items.push_back(entries[i].instance); }
	return items;
}

int InventoryManagerComponent::get_stack_count(const Ref<InventoryItemInstance>& instance) const
{
	const int index = find_entry_index(instance);
	return index >= 0 ? entries[index].count : 0;
}

Ref<InventoryItemInstance> InventoryManagerComponent::find_item_by_id(int64_t item_id) const
{
	const int index = find_entry_index_by_id(item_id);
	return index >= 0 ? entries[index].instance : Ref<InventoryItemInstance>();
}

Ref<InventoryItemInstance> InventoryManagerComponent::find_first_item_stack_by_definition(const Ref<InventoryItemDefinition>& definition) const
{
	for (int i = 0; i < entries.size(); i++)
	{
		if (entries[i].instance->get_item_definition() == definition) { return entries[i].instance; }
	}
	return Ref<InventoryItemInstance>();
}

int InventoryManagerComponent::get_total_item_count_by_definition(const Ref<InventoryItemDefinition>& definition) const
{
	int total = 0;
	for (int i = 0; i < entries.size(); i++)
	{
		if (entries[i].instance->get_item_definition() == definition) { total += entries[i].count; }
	}
	return total;
}

Ref<InventoryItemInstance> InventoryManagerComponent::find_item_anywhere(Node* from, int64_t item_id)
{
	if (from == nullptr || !from->is_inside_tree() || item_id == 0) { return Ref<InventoryItemInstance>(); }
	const TypedArray<Node> inventories = from->get_tree()->get_nodes_in_group(INVENTORIES_GROUP);
	for (int i = 0; i < inventories.size(); i++)
	{
		if (InventoryManagerComponent* inventory = Object::cast_to<InventoryManagerComponent>(inventories[i]))
		{
			Ref<InventoryItemInstance> found = inventory->find_item_by_id(item_id);
			if (found.is_valid()) { return found; }
		}
	}
	return Ref<InventoryItemInstance>();
}

// --- Adding ---

bool InventoryManagerComponent::can_add_item_definition(const Ref<InventoryItemDefinition>& definition, int count) const
{
	if (definition.is_null() || count < 1) { return false; }
	const int max_stack = definition->get_max_stack_size();

	int room = 0;
	for (int i = 0; i < entries.size(); i++)
	{
		if (entries[i].instance->get_item_definition() == definition) { room += MAX(max_stack - entries[i].count, 0); }
	}
	if (room >= count) { return true; }

	const int64_t free_slots = free_entry_slots();
	return int64_t(room) + free_slots * int64_t(max_stack) >= int64_t(count);
}

Ref<InventoryItemInstance> InventoryManagerComponent::create_instance(const Ref<InventoryItemDefinition>& definition)
{
	Ref<InventoryItemInstance> instance;
	instance.instantiate();
	instance->item_id = next_item_id++;
	instance->definition = definition;

	if (ComponentReplication::loadable_path(definition).is_empty() && ComponentReplication::is_sending_server(this, audience_of(replication_mode)))
	{
		WARN_PRINT(vformat("GFGD: item definition \"%s\" is not a saved resource, so clients cannot load it; save it as a .tres.", definition->get_display_name()));
	}

	// Fragments set the starting state before the instance is anybody's.
	const TypedArray<InventoryItemFragment> fragments = definition->get_fragments();
	for (int i = 0; i < fragments.size(); i++)
	{
		Ref<InventoryItemFragment> fragment = fragments[i];
		if (fragment.is_valid()) { fragment->on_instance_created(instance); }
	}
	return instance;
}

Ref<InventoryItemInstance> InventoryManagerComponent::add_item_definition(const Ref<InventoryItemDefinition>& definition, int count)
{
	if (refuse_on_mirror("add_item_definition")) { return Ref<InventoryItemInstance>(); }
	ERR_FAIL_COND_V_MSG(definition.is_null(), Ref<InventoryItemInstance>(), "GFGD: add_item_definition needs a definition.");
	if (count < 1 || free_entry_slots() <= 0) { return Ref<InventoryItemInstance>(); }

	Ref<InventoryItemInstance> instance = create_instance(definition);
	add_item_instance(instance, MIN(count, definition->get_max_stack_size()));
	return instance;
}

int InventoryManagerComponent::add_items(const Ref<InventoryItemDefinition>& definition, int count)
{
	if (refuse_on_mirror("add_items")) { return 0; }
	ERR_FAIL_COND_V_MSG(definition.is_null(), 0, "GFGD: add_items needs a definition.");
	if (count < 1) { return 0; }

	const int max_stack = definition->get_max_stack_size();
	int left = count;
	for (int i = 0; i < entries.size() && left > 0; i++)
	{
		Entry& entry = entries.write[i];
		if (entry.instance->get_item_definition() != definition || entry.count >= max_stack) { continue; }
		const int added = MIN(max_stack - entry.count, left);
		const int old_count = entry.count;
		entry.count += added;
		left -= added;
		dirty_ids.insert(entry.instance->get_item_id());
		emit_signal("stack_changed", entry.instance, old_count, entry.count);
	}
	while (left > 0 && free_entry_slots() > 0)
	{
		const int added = MIN(max_stack, left);
		add_item_instance(create_instance(definition), added);
		left -= added;
	}
	if (left != count)
	{
		queue_flush();
		emit_signal("changed");
	}
	return count - left;
}

bool InventoryManagerComponent::add_item_instance(const Ref<InventoryItemInstance>& instance, int count)
{
	if (refuse_on_mirror("add_item_instance")) { return false; }
	ERR_FAIL_COND_V_MSG(instance.is_null(), false, "GFGD: add_item_instance needs an instance.");
	if (count < 1) { return false; }

	const int existing = find_entry_index(instance);
	if (existing >= 0)
	{
		set_stack_count(instance, entries[existing].count + count);
		return true;
	}
	if (free_entry_slots() <= 0) { return false; }

	// An instance belongs to one inventory at a time.
	if (InventoryManagerComponent* previous = instance->get_inventory())
	{
		if (previous != this) { previous->remove_item_instance(instance); }
	}
	if (instance->item_id == 0) { instance->item_id = next_item_id++; }
	instance->inventory_id = ObjectID(get_instance_id());

	Entry entry;
	entry.instance = instance;
	entry.count = count;
	entries.push_back(entry);

	dirty_ids.insert(instance->get_item_id());
	queue_flush();
	emit_signal("item_added", instance, count);
	emit_signal("stack_changed", instance, 0, count);
	emit_signal("changed");
	return true;
}

// --- Removing ---

void InventoryManagerComponent::remove_entry_at(int index)
{
	const Entry entry = entries[index];
	entries.remove_at(index);
	if (entry.instance->inventory_id == ObjectID(get_instance_id())) { entry.instance->inventory_id = ObjectID(); }

	dirty_ids.erase(entry.instance->get_item_id());
	removed_ids.push_back(entry.instance->get_item_id());
	queue_flush();
	emit_signal("stack_changed", entry.instance, entry.count, 0);
	emit_signal("item_removed", entry.instance);
	emit_signal("changed");
}

bool InventoryManagerComponent::remove_item_instance(const Ref<InventoryItemInstance>& instance)
{
	if (refuse_on_mirror("remove_item_instance")) { return false; }
	const int index = find_entry_index(instance);
	if (index < 0) { return false; }
	remove_entry_at(index);
	return true;
}

void InventoryManagerComponent::set_stack_count(const Ref<InventoryItemInstance>& instance, int count)
{
	if (refuse_on_mirror("set_stack_count")) { return; }
	const int index = find_entry_index(instance);
	if (index < 0) { return; }
	if (count <= 0)
	{
		remove_entry_at(index);
		return;
	}
	Entry& entry = entries.write[index];
	if (entry.count == count) { return; }
	const int old_count = entry.count;
	entry.count = count;
	dirty_ids.insert(instance->get_item_id());
	queue_flush();
	emit_signal("stack_changed", instance, old_count, count);
	emit_signal("changed");
}

bool InventoryManagerComponent::consume_items(const Ref<InventoryItemDefinition>& definition, int count)
{
	if (refuse_on_mirror("consume_items")) { return false; }
	if (definition.is_null() || count < 1) { return false; }
	if (get_total_item_count_by_definition(definition) < count) { return false; }

	int left = count;
	for (int i = entries.size() - 1; i >= 0 && left > 0; i--)
	{
		if (entries[i].instance->get_item_definition() != definition) { continue; }
		const int taken = MIN(entries[i].count, left);
		left -= taken;
		set_stack_count(entries[i].instance, entries[i].count - taken);
	}
	return true;
}

void InventoryManagerComponent::clear()
{
	if (refuse_on_mirror("clear")) { return; }
	while (!entries.is_empty()) { remove_entry_at(entries.size() - 1); }
}

void InventoryManagerComponent::mark_item_dirty(InventoryItemInstance* instance)
{
	if (instance == nullptr || is_mirror()) { return; }
	if (find_entry_index_by_id(instance->get_item_id()) < 0) { return; }
	dirty_ids.insert(instance->get_item_id());
	queue_flush();
	emit_signal("changed");
}

// --- Replication: server ---

void InventoryManagerComponent::queue_flush()
{
	if (flush_queued || !ComponentReplication::is_sending_server(this, audience_of(replication_mode))) { return; }
	flush_queued = true;
	callable_mp(this, &InventoryManagerComponent::flush_replication).call_deferred();
}

Dictionary InventoryManagerComponent::serialize_entry(const Entry& entry) const
{
	Dictionary state = entry.instance->serialize();
	state["count"] = entry.count;
	return state;
}

Array InventoryManagerComponent::build_full_state() const
{
	Array state;
	for (int i = 0; i < entries.size(); i++) { state.push_back(serialize_entry(entries[i])); }
	return state;
}

void InventoryManagerComponent::flush_replication()
{
	flush_queued = false;
	if (!is_inside_tree() || !ComponentReplication::is_sending_server(this, audience_of(replication_mode)))
	{
		dirty_ids.clear();
		removed_ids.clear();
		return;
	}

	// In list order, so a client appends new entries in the order the server has them.
	Array changed;
	for (int i = 0; i < entries.size(); i++)
	{
		if (dirty_ids.has(entries[i].instance->get_item_id())) { changed.push_back(serialize_entry(entries[i])); }
	}
	const PackedInt64Array removed = removed_ids;
	dirty_ids.clear();
	removed_ids.clear();
	if (changed.is_empty() && removed.is_empty()) { return; }

	const PackedInt32Array peers = ComponentReplication::resolve_peers(this, audience_of(replication_mode), subscribers);
	for (int i = 0; i < peers.size(); i++)
	{
		rpc_id(peers[i], "client_update", changed, removed);
	}
}

void InventoryManagerComponent::server_request_full_state()
{
	const ComponentReplication::Audience audience = audience_of(replication_mode);
	if (!ComponentReplication::is_sending_server(this, audience)) { return; }
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	const int sender = multiplayer.is_valid() ? multiplayer->get_remote_sender_id() : 0;
	if (sender <= 0) { return; }
	// A backpack is its owner's business; another peer asking gets nothing.
	if (audience == ComponentReplication::AUDIENCE_OWNER && sender != ComponentReplication::get_owner_peer(this)) { return; }
	subscribers.insert(sender);
	rpc_id(sender, "client_receive_full_state", build_full_state());
}

// --- Replication: client ---

void InventoryManagerComponent::apply_entry(const Dictionary& state)
{
	const int64_t item_id = state.get("id", 0);
	const int count = state.get("count", 0);
	const int index = find_entry_index_by_id(item_id);
	if (index >= 0)
	{
		Entry& entry = entries.write[index];
		entry.instance->apply_state(state);
		if (entry.count != count)
		{
			const int old_count = entry.count;
			entry.count = count;
			emit_signal("stack_changed", entry.instance, old_count, count);
		}
		return;
	}

	Ref<InventoryItemInstance> instance = InventoryItemInstance::deserialize(state);
	instance->inventory_id = ObjectID(get_instance_id());
	Entry entry;
	entry.instance = instance;
	entry.count = count;
	entries.push_back(entry);
	emit_signal("item_added", instance, count);
	emit_signal("stack_changed", instance, 0, count);
}

void InventoryManagerComponent::client_receive_full_state(const Array& state)
{
	// Entries the server no longer has go first, then everything is applied as
	// an update - the same signals as if each change had arrived on its own.
	HashSet<int64_t> kept;
	for (int i = 0; i < state.size(); i++)
	{
		const Dictionary entry = state[i];
		kept.insert(int64_t(entry.get("id", 0)));
	}
	PackedInt64Array removed;
	for (int i = 0; i < entries.size(); i++)
	{
		if (!kept.has(entries[i].instance->get_item_id())) { removed.push_back(entries[i].instance->get_item_id()); }
	}
	client_update(state, removed);
	emit_signal("replicated_state_received");
}

void InventoryManagerComponent::client_update(const Array& changed, const PackedInt64Array& removed)
{
	for (int i = 0; i < removed.size(); i++)
	{
		const int index = find_entry_index_by_id(removed[i]);
		if (index < 0) { continue; }
		const Entry entry = entries[index];
		entries.remove_at(index);
		entry.instance->inventory_id = ObjectID();
		emit_signal("stack_changed", entry.instance, entry.count, 0);
		emit_signal("item_removed", entry.instance);
	}
	for (int i = 0; i < changed.size(); i++)
	{
		apply_entry(changed[i]);
	}
	emit_signal("changed");
}

void InventoryManagerComponent::_bind_methods()
{
	BIND_ENUM_CONSTANT(REPLICATION_NONE);
	BIND_ENUM_CONSTANT(REPLICATION_OWNER);
	BIND_ENUM_CONSTANT(REPLICATION_EVERYONE);

	ClassDB::bind_method(D_METHOD("is_mirror"), &InventoryManagerComponent::is_mirror);
	ClassDB::bind_method(D_METHOD("has_authority"), &InventoryManagerComponent::has_authority);
	ClassDB::bind_method(D_METHOD("can_add_item_definition", "definition", "count"), &InventoryManagerComponent::can_add_item_definition, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("add_item_definition", "definition", "count"), &InventoryManagerComponent::add_item_definition, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("add_items", "definition", "count"), &InventoryManagerComponent::add_items, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("add_item_instance", "instance", "count"), &InventoryManagerComponent::add_item_instance, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("remove_item_instance", "instance"), &InventoryManagerComponent::remove_item_instance);
	ClassDB::bind_method(D_METHOD("set_stack_count", "instance", "count"), &InventoryManagerComponent::set_stack_count);
	ClassDB::bind_method(D_METHOD("consume_items", "definition", "count"), &InventoryManagerComponent::consume_items, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("clear"), &InventoryManagerComponent::clear);
	ClassDB::bind_method(D_METHOD("get_all_items"), &InventoryManagerComponent::get_all_items);
	ClassDB::bind_method(D_METHOD("get_entry_count"), &InventoryManagerComponent::get_entry_count);
	ClassDB::bind_method(D_METHOD("get_stack_count", "instance"), &InventoryManagerComponent::get_stack_count);
	ClassDB::bind_method(D_METHOD("find_item_by_id", "item_id"), &InventoryManagerComponent::find_item_by_id);
	ClassDB::bind_method(D_METHOD("find_first_item_stack_by_definition", "definition"), &InventoryManagerComponent::find_first_item_stack_by_definition);
	ClassDB::bind_method(D_METHOD("get_total_item_count_by_definition", "definition"), &InventoryManagerComponent::get_total_item_count_by_definition);
	ClassDB::bind_static_method("InventoryManagerComponent", D_METHOD("find_item_anywhere", "from", "item_id"), &InventoryManagerComponent::find_item_anywhere);

	ClassDB::bind_method(D_METHOD("set_replication_mode", "value"), &InventoryManagerComponent::set_replication_mode);
	ClassDB::bind_method(D_METHOD("get_replication_mode"), &InventoryManagerComponent::get_replication_mode);
	ClassDB::bind_method(D_METHOD("set_max_entries", "value"), &InventoryManagerComponent::set_max_entries);
	ClassDB::bind_method(D_METHOD("get_max_entries"), &InventoryManagerComponent::get_max_entries);

	ClassDB::bind_method(D_METHOD("server_request_full_state"), &InventoryManagerComponent::server_request_full_state);
	ClassDB::bind_method(D_METHOD("client_receive_full_state", "state"), &InventoryManagerComponent::client_receive_full_state);
	ClassDB::bind_method(D_METHOD("client_update", "changed", "removed"), &InventoryManagerComponent::client_update);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "replication_mode", PROPERTY_HINT_ENUM, "None,Owner,Everyone"), "set_replication_mode", "get_replication_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_entries", PROPERTY_HINT_RANGE, "0,999,1,or_greater"), "set_max_entries", "get_max_entries");

	ADD_SIGNAL(MethodInfo("item_added", PropertyInfo(Variant::OBJECT, "item", PROPERTY_HINT_RESOURCE_TYPE, "InventoryItemInstance"), PropertyInfo(Variant::INT, "count")));
	ADD_SIGNAL(MethodInfo("item_removed", PropertyInfo(Variant::OBJECT, "item", PROPERTY_HINT_RESOURCE_TYPE, "InventoryItemInstance")));
	ADD_SIGNAL(MethodInfo("stack_changed", PropertyInfo(Variant::OBJECT, "item", PROPERTY_HINT_RESOURCE_TYPE, "InventoryItemInstance"), PropertyInfo(Variant::INT, "old_count"), PropertyInfo(Variant::INT, "new_count")));
	ADD_SIGNAL(MethodInfo("changed"));
	ADD_SIGNAL(MethodInfo("replicated_state_received"));
}
}
