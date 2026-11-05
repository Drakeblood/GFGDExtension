#ifndef GFGD_INVENTORY_FRAGMENTS_H
#define GFGD_INVENTORY_FRAGMENTS_H

#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include "inventory/equipment_definition.h"
#include "inventory/inventory_item_definition.h"

using namespace godot;

namespace GFGD
{
// The item can be put on: selecting it in a QuickBarComponent equips this
// definition on the pawn.
class InventoryFragmentEquippableItem : public InventoryItemFragment
{
	GDCLASS(InventoryFragmentEquippableItem, InventoryItemFragment)

private:
	Ref<EquipmentDefinition> equipment_definition;

public:
	void set_equipment_definition(const Ref<EquipmentDefinition>& value) { equipment_definition = value; }
	Ref<EquipmentDefinition> get_equipment_definition() const { return equipment_definition; }

protected:
	static void _bind_methods();
};

// How the item looks in a quick bar slot. Also what makes a QuickBarComponent
// with auto_slot_new_items put the item into a slot by itself.
class InventoryFragmentQuickBarIcon : public InventoryItemFragment
{
	GDCLASS(InventoryFragmentQuickBarIcon, InventoryItemFragment)

private:
	Ref<Texture2D> icon;
	Color tint = Color(1, 1, 1, 1);
	String display_name_when_equipped;
	StringName count_stat_tag;

public:
	void set_icon(const Ref<Texture2D>& value) { icon = value; }
	Ref<Texture2D> get_icon() const { return icon; }

	void set_tint(const Color& value) { tint = value; }
	Color get_tint() const { return tint; }

	// Shown instead of the item's display name while it is the active slot.
	void set_display_name_when_equipped(const String& value) { display_name_when_equipped = value; }
	String get_display_name_when_equipped() const { return display_name_when_equipped; }

	// A stat tag the slot shows as a number (ammo, charges). Empty shows the
	// stack size when it is above one.
	void set_count_stat_tag(const StringName& value) { count_stat_tag = value; }
	StringName get_count_stat_tag() const { return count_stat_tag; }

protected:
	static void _bind_methods();
};

// Starting stat tag stacks of every new instance, e.g. {"Inventory.Charges": 3}.
class InventoryFragmentSetStats : public InventoryItemFragment
{
	GDCLASS(InventoryFragmentSetStats, InventoryItemFragment)

private:
	Dictionary initial_stats;

public:
	void set_initial_stats(const Dictionary& value) { initial_stats = value; }
	Dictionary get_initial_stats() const { return initial_stats; }

	int get_initial_stat(const StringName& tag) const { return int(initial_stats.get(tag, 0)); }

	virtual void on_instance_created(const Ref<InventoryItemInstance>& instance) const override;

protected:
	static void _bind_methods();
};

// The item has a body in the world: the scene a game spawns when the item is
// dropped, and what a pickup of it looks like.
class InventoryFragmentPickup : public InventoryItemFragment
{
	GDCLASS(InventoryFragmentPickup, InventoryItemFragment)

private:
	Ref<PackedScene> scene;
	String display_name;

public:
	void set_scene(const Ref<PackedScene>& value) { scene = value; }
	Ref<PackedScene> get_scene() const { return scene; }

	void set_display_name(const String& value) { display_name = value; }
	String get_display_name() const { return display_name; }

protected:
	static void _bind_methods();
};
}

#endif
