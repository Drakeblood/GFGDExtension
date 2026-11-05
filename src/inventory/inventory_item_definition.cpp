#include "inventory/inventory_item_definition.h"

#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "inventory/inventory_item_instance.h"

namespace GFGD
{
// --- InventoryItemFragment ---

void InventoryItemFragment::on_instance_created(const Ref<InventoryItemInstance>& instance) const
{
	GDVIRTUAL_CALL(_on_instance_created, instance);
}

void InventoryItemFragment::_bind_methods()
{
	GDVIRTUAL_BIND(_on_instance_created, "instance");
}

// --- InventoryItemDefinition ---

bool InventoryItemDefinition::object_is_of_class(const Object* object, const Variant& type)
{
	if (object == nullptr) { return false; }

	if (type.get_type() == Variant::STRING_NAME || type.get_type() == Variant::STRING)
	{
		const StringName name = type;
		if (object->is_class(name)) { return true; }

		// A script class: walk the script chain comparing global names.
		Ref<Script> script = object->get_script();
		while (script.is_valid())
		{
			if (script->get_global_name() == name) { return true; }
			script = script->get_base_script();
		}
		return false;
	}

	Ref<Script> wanted = type;
	if (wanted.is_null()) { return false; }
	Ref<Script> script = object->get_script();
	while (script.is_valid())
	{
		if (script == wanted) { return true; }
		script = script->get_base_script();
	}
	return false;
}

Ref<InventoryItemFragment> InventoryItemDefinition::find_fragment_by_class(const Variant& fragment_class) const
{
	for (int i = 0; i < fragments.size(); i++)
	{
		Ref<InventoryItemFragment> fragment = fragments[i];
		if (fragment.is_valid() && object_is_of_class(fragment.ptr(), fragment_class))
		{
			return fragment;
		}
	}
	return Ref<InventoryItemFragment>();
}

void InventoryItemDefinition::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_display_name", "value"), &InventoryItemDefinition::set_display_name);
	ClassDB::bind_method(D_METHOD("get_display_name"), &InventoryItemDefinition::get_display_name);
	ClassDB::bind_method(D_METHOD("set_max_stack_size", "value"), &InventoryItemDefinition::set_max_stack_size);
	ClassDB::bind_method(D_METHOD("get_max_stack_size"), &InventoryItemDefinition::get_max_stack_size);
	ClassDB::bind_method(D_METHOD("set_fragments", "value"), &InventoryItemDefinition::set_fragments);
	ClassDB::bind_method(D_METHOD("get_fragments"), &InventoryItemDefinition::get_fragments);
	ClassDB::bind_method(D_METHOD("find_fragment_by_class", "fragment_class"), &InventoryItemDefinition::find_fragment_by_class);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name"), "set_display_name", "get_display_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_stack_size", PROPERTY_HINT_RANGE, "1,9999,1,or_greater"), "set_max_stack_size", "get_max_stack_size");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "fragments", PROPERTY_HINT_ARRAY_TYPE, vformat("%d/%d:InventoryItemFragment", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_fragments", "get_fragments");
}
}
