#include "inventory/inventory_item_instance.h"

#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "inventory/component_replication.h"
#include "inventory/inventory_item_definition.h"
#include "inventory/inventory_manager_component.h"

namespace GFGD
{
InventoryManagerComponent* InventoryItemInstance::get_inventory() const
{
	return Object::cast_to<InventoryManagerComponent>(ObjectDB::get_instance(inventory_id));
}

bool InventoryItemInstance::can_write() const
{
	InventoryManagerComponent* inventory = get_inventory();
	if (inventory != nullptr && inventory->is_mirror())
	{
		ERR_PRINT("GFGD: an item instance on a client is a mirror of the server's; change it on the server.");
		return false;
	}
	return true;
}

void InventoryItemInstance::notify_changed()
{
	if (InventoryManagerComponent* inventory = get_inventory())
	{
		inventory->mark_item_dirty(this);
	}
}

Ref<InventoryItemDefinition> InventoryItemInstance::get_item_definition() const
{
	return definition;
}

Ref<InventoryItemFragment> InventoryItemInstance::find_fragment_by_class(const Variant& fragment_class) const
{
	const Ref<InventoryItemDefinition> item_definition = definition;
	return item_definition.is_valid() ? item_definition->find_fragment_by_class(fragment_class) : Ref<InventoryItemFragment>();
}

void InventoryItemInstance::add_stat_tag_stack(const StringName& tag, int count)
{
	if (count < 1 || !can_write()) { return; }
	set_stat_tag_stack(tag, get_stat_tag_stack_count(tag) + count);
}

void InventoryItemInstance::remove_stat_tag_stack(const StringName& tag, int count)
{
	if (count < 1 || !can_write()) { return; }
	set_stat_tag_stack(tag, get_stat_tag_stack_count(tag) - count);
}

void InventoryItemInstance::set_stat_tag_stack(const StringName& tag, int count)
{
	if (!can_write()) { return; }
	count = MAX(count, 0);
	if (get_stat_tag_stack_count(tag) == count) { return; }
	if (count == 0) { stat_tags.erase(tag); }
	else { stat_tags[tag] = count; }
	emit_signal("stat_tag_changed", tag, count);
	notify_changed();
}

int InventoryItemInstance::get_stat_tag_stack_count(const StringName& tag) const
{
	return int(stat_tags.get(tag, 0));
}

bool InventoryItemInstance::has_stat_tag(const StringName& tag) const
{
	return get_stat_tag_stack_count(tag) > 0;
}

void InventoryItemInstance::set_data(const StringName& key, const Variant& value)
{
	if (!can_write()) { return; }
	if (data.has(key) && data[key] == value) { return; }
	if (value.get_type() == Variant::NIL) { data.erase(key); }
	else { data[key] = value; }
	emit_signal("data_changed", key, value);
	notify_changed();
}

Variant InventoryItemInstance::get_data(const StringName& key, const Variant& default_value) const
{
	return data.get(key, default_value);
}

String InventoryItemInstance::get_debug_string() const
{
	const Ref<InventoryItemDefinition> item_definition = definition;
	const String name = item_definition.is_valid() ? item_definition->get_display_name() : String("<no definition>");
	return vformat("#%d %s", item_id, name);
}

Dictionary InventoryItemInstance::serialize() const
{
	Dictionary state;
	state["id"] = item_id;
	state["def"] = ComponentReplication::loadable_path(definition);
	state["stats"] = stat_tags;
	state["data"] = data;
	return state;
}

Ref<InventoryItemInstance> InventoryItemInstance::deserialize(const Dictionary& state)
{
	Ref<InventoryItemInstance> instance;
	instance.instantiate();
	instance->item_id = int64_t(state.get("id", 0));
	const String path = state.get("def", String());
	if (!path.is_empty())
	{
		instance->definition = ResourceLoader::get_singleton()->load(path);
	}
	instance->apply_state(state);
	return instance;
}

void InventoryItemInstance::apply_state(const Dictionary& state)
{
	const Dictionary new_stats = state.get("stats", Dictionary());
	const Dictionary new_data = state.get("data", Dictionary());

	// Signals for what changed, the same ones the server's instance emitted.
	const Dictionary old_stats = stat_tags;
	const Dictionary old_data = data;
	stat_tags = new_stats.duplicate();
	data = new_data.duplicate();

	const Array stat_keys = new_stats.keys();
	for (int i = 0; i < stat_keys.size(); i++)
	{
		if (old_stats.get(stat_keys[i], 0) != new_stats[stat_keys[i]]) { emit_signal("stat_tag_changed", stat_keys[i], new_stats[stat_keys[i]]); }
	}
	const Array old_stat_keys = old_stats.keys();
	for (int i = 0; i < old_stat_keys.size(); i++)
	{
		if (!new_stats.has(old_stat_keys[i])) { emit_signal("stat_tag_changed", old_stat_keys[i], 0); }
	}
	const Array data_keys = new_data.keys();
	for (int i = 0; i < data_keys.size(); i++)
	{
		if (!old_data.has(data_keys[i]) || old_data[data_keys[i]] != new_data[data_keys[i]]) { emit_signal("data_changed", data_keys[i], new_data[data_keys[i]]); }
	}
	const Array old_data_keys = old_data.keys();
	for (int i = 0; i < old_data_keys.size(); i++)
	{
		if (!new_data.has(old_data_keys[i])) { emit_signal("data_changed", old_data_keys[i], Variant()); }
	}
}

void InventoryItemInstance::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_item_id"), &InventoryItemInstance::get_item_id);
	ClassDB::bind_method(D_METHOD("get_item_definition"), &InventoryItemInstance::get_item_definition);
	ClassDB::bind_method(D_METHOD("get_inventory"), &InventoryItemInstance::get_inventory);
	ClassDB::bind_method(D_METHOD("find_fragment_by_class", "fragment_class"), &InventoryItemInstance::find_fragment_by_class);
	ClassDB::bind_method(D_METHOD("add_stat_tag_stack", "tag", "count"), &InventoryItemInstance::add_stat_tag_stack);
	ClassDB::bind_method(D_METHOD("remove_stat_tag_stack", "tag", "count"), &InventoryItemInstance::remove_stat_tag_stack);
	ClassDB::bind_method(D_METHOD("set_stat_tag_stack", "tag", "count"), &InventoryItemInstance::set_stat_tag_stack);
	ClassDB::bind_method(D_METHOD("get_stat_tag_stack_count", "tag"), &InventoryItemInstance::get_stat_tag_stack_count);
	ClassDB::bind_method(D_METHOD("has_stat_tag", "tag"), &InventoryItemInstance::has_stat_tag);
	ClassDB::bind_method(D_METHOD("get_stat_tags"), &InventoryItemInstance::get_stat_tags);
	ClassDB::bind_method(D_METHOD("set_data", "key", "value"), &InventoryItemInstance::set_data);
	ClassDB::bind_method(D_METHOD("get_data", "key", "default_value"), &InventoryItemInstance::get_data, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("has_data", "key"), &InventoryItemInstance::has_data);
	ClassDB::bind_method(D_METHOD("get_all_data"), &InventoryItemInstance::get_all_data);
	ClassDB::bind_method(D_METHOD("get_debug_string"), &InventoryItemInstance::get_debug_string);

	ADD_SIGNAL(MethodInfo("stat_tag_changed", PropertyInfo(Variant::STRING_NAME, "tag"), PropertyInfo(Variant::INT, "count")));
	ADD_SIGNAL(MethodInfo("data_changed", PropertyInfo(Variant::STRING_NAME, "key"), PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
}
}
