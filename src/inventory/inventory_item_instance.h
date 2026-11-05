#ifndef GFGD_INVENTORY_ITEM_INSTANCE_H
#define GFGD_INVENTORY_ITEM_INSTANCE_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>


using namespace godot;

namespace GFGD
{
class InventoryManagerComponent;
class InventoryItemDefinition;
class InventoryItemFragment;

// One item somebody has: which definition it is, plus the state this copy
// carries - stat tag stacks (ammo, charges, durability) and free-form data.
//
// Created by an InventoryManagerComponent on the server. A client's mirror has
// the same item_id, so the server and client can name it to each other, and it
// is read-only there: the setters refuse on a mirror.
class InventoryItemInstance : public RefCounted
{
	GDCLASS(InventoryItemInstance, RefCounted)

	friend class InventoryManagerComponent;

private:
	int64_t item_id = 0;
	// Held as a Resource so this header need not include the definition's,
	// which includes this one for its fragments.
	Ref<Resource> definition;
	Dictionary stat_tags;
	Dictionary data;
	ObjectID inventory_id;

	bool can_write() const;
	void notify_changed();

public:
	int64_t get_item_id() const { return item_id; }
	Ref<InventoryItemDefinition> get_item_definition() const;

	// The inventory holding this instance right now, or null.
	InventoryManagerComponent* get_inventory() const;

	Ref<InventoryItemFragment> find_fragment_by_class(const Variant& fragment_class) const;

	// --- Stat tags: named counters, e.g. "Inventory.Ammo" = 30 ---
	// Adding or removing less than one stack does nothing.
	void add_stat_tag_stack(const StringName& tag, int count);
	void remove_stat_tag_stack(const StringName& tag, int count);
	void set_stat_tag_stack(const StringName& tag, int count);
	int get_stat_tag_stack_count(const StringName& tag) const;
	bool has_stat_tag(const StringName& tag) const;
	Dictionary get_stat_tags() const { return stat_tags.duplicate(); }

	// --- Free-form per-instance data (colour, owner name, contents) ---
	// Values must be things an RPC can carry - no objects.
	void set_data(const StringName& key, const Variant& value);
	Variant get_data(const StringName& key, const Variant& default_value = Variant()) const;
	bool has_data(const StringName& key) const { return data.has(key); }
	Dictionary get_all_data() const { return data.duplicate(); }

	String get_debug_string() const;

	// What replication sends of an instance, and builds it back from.
	Dictionary serialize() const;
	static Ref<InventoryItemInstance> deserialize(const Dictionary& state);
	void apply_state(const Dictionary& state);

protected:
	static void _bind_methods();
};
}

#endif
