#ifndef GFGD_EQUIPMENT_MANAGER_COMPONENT_H
#define GFGD_EQUIPMENT_MANAGER_COMPONENT_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/vector.hpp>

#include "inventory/equipment_instance.h"

using namespace godot;

namespace GFGD
{
class AbilitySystemComponent;

// What a pawn has on. Sits beside the Pawn under the pawn's root, next to the
// AbilitySystemComponent it grants abilities through.
//
// The server equips and unequips; every peer in replication_mode (everyone by
// default - you see the axe in a friend's hand) gets the same EquipmentInstance
// with the same scenes spawned.
class EquipmentManagerComponent : public Node
{
	GDCLASS(EquipmentManagerComponent, Node)

	// Server: peers that asked for the full state; changes go to them.
	HashSet<int> subscribers;

public:
	enum ReplicationMode
	{
		REPLICATION_NONE = 0,
		REPLICATION_OWNER = 1,
		REPLICATION_EVERYONE = 2,
	};

private:
	Vector<Ref<EquipmentInstance>> instances;
	ReplicationMode replication_mode = REPLICATION_EVERYONE;
	int64_t next_equipment_id = 1;

	bool is_sending_server() const;
	bool refuse_on_mirror(const char* what) const;
	Ref<EquipmentInstance> create_instance(const Ref<EquipmentDefinition>& definition, int64_t equipment_id);
	Dictionary serialize_instance(const Ref<EquipmentInstance>& instance) const;
	void send_to_audience(const StringName& method, const Variant& argument);
	void grant_server_side(const Ref<EquipmentInstance>& instance);
	void revoke_server_side(const Ref<EquipmentInstance>& instance);
	void remove_instance_at(int index, bool server_side);

public:
	virtual void _ready() override;
	virtual void _exit_tree() override;

	bool is_mirror() const;

	// The AbilitySystemComponent beside this one, if any.
	AbilitySystemComponent* get_ability_system_component() const;

	// Server: puts the equipment on. instigator is what it came from - usually
	// the InventoryItemInstance - and is handed to granted abilities as their
	// source object.
	Ref<EquipmentInstance> equip_item(const Ref<EquipmentDefinition>& definition, const Variant& instigator = Variant());
	void unequip_item(const Ref<EquipmentInstance>& instance);
	void unequip_all();

	Array get_equipment_instances() const;

	// The first instance whose script is (or extends) the given class - a
	// class_name, a script, or a native class name.
	Ref<EquipmentInstance> get_first_instance_of_type(const Variant& instance_class) const;
	Ref<EquipmentInstance> find_instance_by_definition(const Ref<EquipmentDefinition>& definition) const;
	Ref<EquipmentInstance> find_instance_by_item_id(int64_t item_id) const;

	void set_replication_mode(ReplicationMode value) { replication_mode = value; }
	ReplicationMode get_replication_mode() const { return replication_mode; }

	// --- RPC endpoints ---
	void server_request_full_state();
	void client_receive_full_state(const Array& state);
	void client_equipped(const Dictionary& state);
	void client_unequipped(int64_t equipment_id);

protected:
	static void _bind_methods();
};
}

VARIANT_ENUM_CAST(GFGD::EquipmentManagerComponent::ReplicationMode);

#endif
