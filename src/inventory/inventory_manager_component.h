#ifndef GFGD_INVENTORY_MANAGER_COMPONENT_H
#define GFGD_INVENTORY_MANAGER_COMPONENT_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>

#include "inventory/inventory_item_definition.h"

using namespace godot;

namespace GFGD
{
// A list of item stacks: each entry is one InventoryItemInstance and how many of
// it there are. Put it under a PlayerController for a player's backpack (it then
// outlives the pawn), under a pawn, or on a chest in the level.
//
// The server's list is the truth. Clients get a read-only mirror, sent to the
// owner or to everyone (replication_mode); every change on the server reaches
// them once per frame, and the same signals fire on both sides.
class InventoryManagerComponent : public Node
{
	GDCLASS(InventoryManagerComponent, Node)

	// Server: peers that asked for the full state; changes go to them.
	HashSet<int> subscribers;

public:
	enum ReplicationMode
	{
		// Every peer keeps its own list, as offline.
		REPLICATION_NONE = 0,
		// The peer owning the parent - a player's own backpack.
		REPLICATION_OWNER = 1,
		// Every peer - a chest everyone may look into.
		REPLICATION_EVERYONE = 2,
	};

	// Every inventory in the tree, so tools and lookups by item id need no tree search.
	static constexpr const char* INVENTORIES_GROUP = "_gfgd_inventories";

private:
	struct Entry
	{
		Ref<InventoryItemInstance> instance;
		int count = 0;
	};

	Vector<Entry> entries;
	ReplicationMode replication_mode = REPLICATION_OWNER;
	int max_entries = 0;

	// Server side: what changed since the last flush.
	HashSet<int64_t> dirty_ids;
	PackedInt64Array removed_ids;
	bool flush_queued = false;

	// Unique across every inventory, so an instance keeps its id when it moves
	// from one to another.
	static int64_t next_item_id;

	int find_entry_index(const Ref<InventoryItemInstance>& instance) const;
	int find_entry_index_by_id(int64_t item_id) const;
	int free_entry_slots() const;
	bool refuse_on_mirror(const char* what) const;
	void queue_flush();
	void flush_replication();
	Dictionary serialize_entry(const Entry& entry) const;
	Array build_full_state() const;
	void apply_entry(const Dictionary& state);
	void remove_entry_at(int index);
	Ref<InventoryItemInstance> create_instance(const Ref<InventoryItemDefinition>& definition);

public:
	virtual void _ready() override;

	bool is_mirror() const;
	bool has_authority() const { return !is_mirror(); }

	// --- Adding ---

	// Whether count units of the definition fit, topping up existing stacks
	// first. Always true without max_entries and with unlimited stacks.
	bool can_add_item_definition(const Ref<InventoryItemDefinition>& definition, int count = 1) const;

	// One new entry of count units (capped at the definition's max stack size),
	// or null when the inventory is full. Never merges into an existing stack:
	// two of the same definition are two instances, each with its own state.
	Ref<InventoryItemInstance> add_item_definition(const Ref<InventoryItemDefinition>& definition, int count = 1);

	// count units, topping up existing stacks before starting new ones. Returns
	// how many fitted.
	int add_items(const Ref<InventoryItemDefinition>& definition, int count = 1);

	// Takes in an instance that already exists, e.g. one taken out of another
	// inventory - its stats and data come with it.
	bool add_item_instance(const Ref<InventoryItemInstance>& instance, int count = 1);

	// --- Removing ---

	bool remove_item_instance(const Ref<InventoryItemInstance>& instance);

	// Sets a stack's size; 0 removes the entry.
	void set_stack_count(const Ref<InventoryItemInstance>& instance, int count);

	// Takes count units of the definition from its stacks, newest first. All or
	// nothing: with fewer than count units it takes none and returns false.
	bool consume_items(const Ref<InventoryItemDefinition>& definition, int count = 1);

	void clear();

	// --- Reading (on every peer that has a mirror) ---

	Array get_all_items() const;
	int get_entry_count() const { return entries.size(); }
	int get_stack_count(const Ref<InventoryItemInstance>& instance) const;
	Ref<InventoryItemInstance> find_item_by_id(int64_t item_id) const;
	Ref<InventoryItemInstance> find_first_item_stack_by_definition(const Ref<InventoryItemDefinition>& definition) const;

	// Units, not entries: two stacks of 5 are 10.
	int get_total_item_count_by_definition(const Ref<InventoryItemDefinition>& definition) const;

	// The instance with this id in any inventory in the tree of `from`.
	static Ref<InventoryItemInstance> find_item_anywhere(Node* from, int64_t item_id);

	// Called by an instance whose stats or data changed on the server.
	void mark_item_dirty(InventoryItemInstance* instance);

	void set_replication_mode(ReplicationMode value) { replication_mode = value; }
	ReplicationMode get_replication_mode() const { return replication_mode; }
	void set_max_entries(int value) { max_entries = MAX(value, 0); }
	int get_max_entries() const { return max_entries; }

	// --- RPC endpoints ---
	void server_request_full_state();
	void client_receive_full_state(const Array& state);
	void client_update(const Array& changed, const PackedInt64Array& removed);

protected:
	static void _bind_methods();
};
}

VARIANT_ENUM_CAST(GFGD::InventoryManagerComponent::ReplicationMode);

#endif
