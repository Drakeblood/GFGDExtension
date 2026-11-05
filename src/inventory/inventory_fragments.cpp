#include "inventory/inventory_fragments.h"

#include <godot_cpp/core/class_db.hpp>

#include "inventory/inventory_item_instance.h"

namespace GFGD
{
void InventoryFragmentEquippableItem::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_equipment_definition", "value"), &InventoryFragmentEquippableItem::set_equipment_definition);
	ClassDB::bind_method(D_METHOD("get_equipment_definition"), &InventoryFragmentEquippableItem::get_equipment_definition);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "equipment_definition", PROPERTY_HINT_RESOURCE_TYPE, "EquipmentDefinition"), "set_equipment_definition", "get_equipment_definition");
}

void InventoryFragmentQuickBarIcon::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_icon", "value"), &InventoryFragmentQuickBarIcon::set_icon);
	ClassDB::bind_method(D_METHOD("get_icon"), &InventoryFragmentQuickBarIcon::get_icon);
	ClassDB::bind_method(D_METHOD("set_tint", "value"), &InventoryFragmentQuickBarIcon::set_tint);
	ClassDB::bind_method(D_METHOD("get_tint"), &InventoryFragmentQuickBarIcon::get_tint);
	ClassDB::bind_method(D_METHOD("set_display_name_when_equipped", "value"), &InventoryFragmentQuickBarIcon::set_display_name_when_equipped);
	ClassDB::bind_method(D_METHOD("get_display_name_when_equipped"), &InventoryFragmentQuickBarIcon::get_display_name_when_equipped);
	ClassDB::bind_method(D_METHOD("set_count_stat_tag", "value"), &InventoryFragmentQuickBarIcon::set_count_stat_tag);
	ClassDB::bind_method(D_METHOD("get_count_stat_tag"), &InventoryFragmentQuickBarIcon::get_count_stat_tag);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "icon", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_icon", "get_icon");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "tint"), "set_tint", "get_tint");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name_when_equipped"), "set_display_name_when_equipped", "get_display_name_when_equipped");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "count_stat_tag"), "set_count_stat_tag", "get_count_stat_tag");
}

void InventoryFragmentSetStats::on_instance_created(const Ref<InventoryItemInstance>& instance) const
{
	const Array keys = initial_stats.keys();
	for (int i = 0; i < keys.size(); i++)
	{
		instance->add_stat_tag_stack(StringName(keys[i]), int(initial_stats[keys[i]]));
	}
	InventoryItemFragment::on_instance_created(instance);
}

void InventoryFragmentSetStats::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_initial_stats", "value"), &InventoryFragmentSetStats::set_initial_stats);
	ClassDB::bind_method(D_METHOD("get_initial_stats"), &InventoryFragmentSetStats::get_initial_stats);
	ClassDB::bind_method(D_METHOD("get_initial_stat", "tag"), &InventoryFragmentSetStats::get_initial_stat);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "initial_stats"), "set_initial_stats", "get_initial_stats");
}

void InventoryFragmentPickup::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_scene", "value"), &InventoryFragmentPickup::set_scene);
	ClassDB::bind_method(D_METHOD("get_scene"), &InventoryFragmentPickup::get_scene);
	ClassDB::bind_method(D_METHOD("set_display_name", "value"), &InventoryFragmentPickup::set_display_name);
	ClassDB::bind_method(D_METHOD("get_display_name"), &InventoryFragmentPickup::get_display_name);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_scene", "get_scene");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name"), "set_display_name", "get_display_name");
}
}
