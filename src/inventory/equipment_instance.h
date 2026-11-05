#ifndef GFGD_EQUIPMENT_INSTANCE_H
#define GFGD_EQUIPMENT_INSTANCE_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>

#include "inventory/equipment_definition.h"
#include "inventory/inventory_item_definition.h"

using namespace godot;

namespace GFGD
{
class EquipmentManagerComponent;

// One piece of equipment that is on a pawn right now. Exists on every peer the
// EquipmentManagerComponent replicates to, with the same equipment_id, so each
// of them shows the spawned scenes and runs _on_equipped / _on_unequipped.
// Granting abilities, effects and tags is the server's part and happens before
// _on_equipped runs there.
//
// Extend it in a script (EquipmentDefinition.instance_script) for an item with
// logic of its own - a lamp that switches its light on, a tool that changes the
// character's pose.
class EquipmentInstance : public RefCounted
{
	GDCLASS(EquipmentInstance, RefCounted)

	friend class EquipmentManagerComponent;

private:
	int64_t equipment_id = 0;
	int64_t item_id = 0;
	Ref<EquipmentDefinition> definition;
	Ref<InventoryItemDefinition> item_definition;
	Variant instigator;
	ObjectID manager_id;
	ObjectID pawn_root_id;
	PackedInt64Array spawned_node_ids;
	bool spawned_visible = true;

	// Server only: what to take back on unequip.
	PackedInt64Array granted_ability_handles;
	PackedInt64Array applied_effect_ids;

	Node* resolve_attach_parent(const Ref<EquipmentActorToSpawn>& spawn);

public:
	int64_t get_equipment_id() const { return equipment_id; }

	// The id of the InventoryItemInstance this came from (0 when it was put on
	// without an item), the same on every peer.
	int64_t get_item_id() const { return item_id; }

	Ref<EquipmentDefinition> get_equipment_definition() const { return definition; }
	Ref<InventoryItemDefinition> get_item_definition() const { return item_definition; }

	// Whatever equipped it - on the server the InventoryItemInstance; on a client
	// the mirror of that instance when this peer has the inventory, else null.
	Variant get_instigator() const { return instigator; }

	// The node the manager sits under: the pawn's root.
	Node* get_pawn() const;
	EquipmentManagerComponent* get_manager() const;

	Array get_spawned_nodes() const;

	// Shows or hides every spawned scene, e.g. the axe while both hands carry a log.
	void set_spawned_visible(bool value);
	bool is_spawned_visible() const { return spawned_visible; }

	virtual void spawn_equipment_actors();
	virtual void destroy_equipment_actors();
	virtual void on_equipped();
	virtual void on_unequipped();

protected:
	static void _bind_methods();

	GDVIRTUAL0(_on_equipped)
	GDVIRTUAL0(_on_unequipped)
	// Return the node a spawned scene goes under, or null for the default
	// (attach_to and attach_bone of the spawn entry).
	GDVIRTUAL1R(Node*, _get_attach_parent, Ref<EquipmentActorToSpawn>)
};
}

#endif
