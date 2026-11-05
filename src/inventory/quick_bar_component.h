#ifndef GFGD_QUICK_BAR_COMPONENT_H
#define GFGD_QUICK_BAR_COMPONENT_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>

#include "inventory/equipment_instance.h"
#include "inventory/inventory_item_instance.h"

using namespace godot;

namespace GFGD
{
class EquipmentManagerComponent;
class InventoryManagerComponent;

// A row of slots pointing at items of an inventory, one of them active. The
// active slot's item is equipped on the pawn (InventoryFragmentEquippableItem),
// any other slot's is not.
//
// Put it beside the InventoryManagerComponent - under a PlayerController the
// pair outlives the pawn, and the active item is put on again whenever the
// controller possesses a new one. Under a pawn root it equips on that pawn.
//
// The server decides. A client calling set_active_slot_index or swap_slots
// sends the request to the server and sees the result when it comes back; the
// slots reach the owner only.
class QuickBarComponent : public Node
{
	GDCLASS(QuickBarComponent, Node)

	// Server: peers that asked for the full state; changes go to them.
	HashSet<int> subscribers;

private:
	int num_slots = 3;
	bool allow_no_active_slot = true;
	bool auto_slot_new_items = true;
	bool replicated = true;
	NodePath inventory_path;

	PackedInt64Array slot_ids;
	int active_slot_index = -1;

	// Server: what the active slot put on, and on which manager.
	Ref<EquipmentInstance> equipped;
	ObjectID equipped_manager_id;
	bool flush_queued = false;

	void ensure_slot_count();
	bool refuse_on_mirror(const char* what) const;
	void queue_flush();
	void flush_replication();
	void equip_item_in_slot();
	void unequip_item_in_slot();
	void apply_active_slot_index(int index);
	void apply_swap(int from, int to);
	void connect_inventory();
	void _on_inventory_item_added(const Ref<InventoryItemInstance>& item, int count);
	void _on_inventory_item_removed(const Ref<InventoryItemInstance>& item);
	void _on_inventory_changed();
	void _on_pawn_changed(Object* pawn);

public:
	virtual void _ready() override;

	bool is_mirror() const;

	InventoryManagerComponent* get_inventory() const;
	EquipmentManagerComponent* get_equipment_manager() const;

	// --- Active slot (callable on the owning client too) ---

	// -1 puts nothing on (when allow_no_active_slot). Selecting the active slot
	// again changes nothing.
	void set_active_slot_index(int index);
	int get_active_slot_index() const { return active_slot_index; }

	// The next / previous slot holding an item, wrapping around.
	void cycle_active_slot_forward();
	void cycle_active_slot_backward();

	// Swaps two slots' items, keeping the active index where it was.
	void swap_slots(int from, int to);

	// --- Slots ---

	// One entry per slot: the InventoryItemInstance, or null for an empty slot.
	Array get_slots() const;
	Ref<InventoryItemInstance> get_slot_item(int index) const;
	Ref<InventoryItemInstance> get_active_slot_item() const;
	bool is_item_slot_free(int index) const;
	int get_next_free_item_slot() const;
	int find_slot_of_item(const Ref<InventoryItemInstance>& item) const;

	// Server: puts an item of the inventory into an empty slot.
	bool add_item_to_slot(int index, const Ref<InventoryItemInstance>& item);
	// Server: empties a slot, unequipping it if it was active. Returns the item.
	Ref<InventoryItemInstance> remove_item_from_slot(int index);
	// Server: empties every slot and deselects.
	void reset_slots();

	// What the active slot has on, read from the pawn's EquipmentManagerComponent
	// - available wherever the equipment replicates.
	Ref<EquipmentInstance> get_equipped_item() const;

	void set_num_slots(int value);
	int get_num_slots() const { return num_slots; }
	void set_allow_no_active_slot(bool value) { allow_no_active_slot = value; }
	bool get_allow_no_active_slot() const { return allow_no_active_slot; }
	void set_auto_slot_new_items(bool value) { auto_slot_new_items = value; }
	bool get_auto_slot_new_items() const { return auto_slot_new_items; }
	void set_replicated(bool value) { replicated = value; }
	bool get_replicated() const { return replicated; }
	void set_inventory_path(const NodePath& value) { inventory_path = value; }
	NodePath get_inventory_path() const { return inventory_path; }

	// --- RPC endpoints ---
	void server_request_full_state();
	void server_set_active_slot_index(int index);
	void server_swap_slots(int from, int to);
	void client_receive_state(const PackedInt64Array& slots, int active_index);

protected:
	static void _bind_methods();
};
}

#endif
