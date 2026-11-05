#ifndef GFGD_INVENTORY_ITEM_DEFINITION_H
#define GFGD_INVENTORY_ITEM_DEFINITION_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/typed_array.hpp>

#include "inventory/inventory_item_instance.h"

using namespace godot;

namespace GFGD
{
// One piece of what an item is. A definition carries no behaviour of its own:
// whether the item has a quick bar icon, can be equipped, starts with stats or
// drops as a scene is decided by which fragments it lists. A game adds its own
// by extending this class in a script.
class InventoryItemFragment : public Resource
{
	GDCLASS(InventoryItemFragment, Resource)

public:
	// Called on the server for every new instance of the item, before anybody
	// is told the instance exists - the place to set its starting stats.
	virtual void on_instance_created(const Ref<InventoryItemInstance>& instance) const;

protected:
	static void _bind_methods();

	GDVIRTUAL1C(_on_instance_created, Ref<InventoryItemInstance>)
};

// What an item is: its name, how many fit in one stack, and its fragments.
// Shared by every instance, so it is never written at runtime - per-instance
// state lives on the InventoryItemInstance.
//
// A definition that should reach clients has to be a saved resource (.tres):
// replication sends its path and the client loads the same file.
class InventoryItemDefinition : public Resource
{
	GDCLASS(InventoryItemDefinition, Resource)

private:
	String display_name;
	int max_stack_size = 1;
	TypedArray<InventoryItemFragment> fragments;

public:
	void set_display_name(const String& value) { display_name = value; }
	String get_display_name() const { return display_name; }

	void set_max_stack_size(int value) { max_stack_size = MAX(value, 1); }
	int get_max_stack_size() const { return max_stack_size; }

	void set_fragments(const TypedArray<InventoryItemFragment>& value) { fragments = value; }
	TypedArray<InventoryItemFragment> get_fragments() const { return fragments; }

	// The first fragment of the given type: a native class name
	// (&"InventoryFragmentQuickBarIcon"), a script's class_name, or the script
	// itself (preload("my_fragment.gd")). Subclasses match too.
	Ref<InventoryItemFragment> find_fragment_by_class(const Variant& fragment_class) const;

	// The same test, for anyone matching objects against a class given that way.
	static bool object_is_of_class(const Object* object, const Variant& type);

protected:
	static void _bind_methods();
};
}

#endif
