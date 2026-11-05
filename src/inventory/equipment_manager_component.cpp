#include "inventory/equipment_manager_component.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_set.hpp>

#include "ability_system/ability_system_component.h"
#include "ability_system/gameplay_ability.h"
#include "ability_system/gameplay_effect.h"
#include "framework/world.h"
#include "gameplay_tags/gameplay_tags_manager.h"
#include "inventory/component_replication.h"
#include "inventory/inventory_item_instance.h"
#include "inventory/inventory_manager_component.h"

namespace GFGD
{
namespace
{
ComponentReplication::Audience audience_of(EquipmentManagerComponent::ReplicationMode mode)
{
	switch (mode)
	{
		case EquipmentManagerComponent::REPLICATION_OWNER: return ComponentReplication::AUDIENCE_OWNER;
		case EquipmentManagerComponent::REPLICATION_EVERYONE: return ComponentReplication::AUDIENCE_EVERYONE;
		default: return ComponentReplication::AUDIENCE_NONE;
	}
}
}

void EquipmentManagerComponent::_ready()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	PackedStringArray from_anyone;
	from_anyone.push_back("server_request_full_state");
	PackedStringArray from_server;
	from_server.push_back("client_receive_full_state");
	from_server.push_back("client_equipped");
	from_server.push_back("client_unequipped");
	ComponentReplication::configure_rpcs(this, from_anyone, from_server);

	if (is_mirror())
	{
		rpc_id(World::SERVER_PEER_ID, "server_request_full_state");
	}
}

void EquipmentManagerComponent::_exit_tree()
{
	// The pawn is going away: scenes leave with it, and on the server granted
	// abilities are taken back while the ASC is still there to take them from.
	for (int i = instances.size() - 1; i >= 0; i--)
	{
		remove_instance_at(i, !is_mirror());
	}
}

bool EquipmentManagerComponent::is_mirror() const
{
	return ComponentReplication::is_mirror(this, audience_of(replication_mode));
}

bool EquipmentManagerComponent::is_sending_server() const
{
	return ComponentReplication::is_sending_server(this, audience_of(replication_mode));
}

bool EquipmentManagerComponent::refuse_on_mirror(const char* what) const
{
	if (!is_mirror()) { return false; }
	ERR_PRINT(vformat("GFGD: %s called on a client's mirror of %s; equipment changes on the server.", String(what), String(get_path())));
	return true;
}

AbilitySystemComponent* EquipmentManagerComponent::get_ability_system_component() const
{
	Node* parent = get_parent();
	if (parent == nullptr) { return nullptr; }
	const int count = parent->get_child_count();
	for (int i = 0; i < count; i++)
	{
		if (AbilitySystemComponent* asc = Object::cast_to<AbilitySystemComponent>(parent->get_child(i))) { return asc; }
	}
	return nullptr;
}

Ref<EquipmentInstance> EquipmentManagerComponent::create_instance(const Ref<EquipmentDefinition>& definition, int64_t equipment_id)
{
	Ref<EquipmentInstance> instance;
	Ref<Script> script = definition.is_valid() ? definition->get_instance_script() : Ref<Script>();
	if (script.is_valid() && script->can_instantiate())
	{
		Ref<EquipmentInstance> base;
		base.instantiate();
		base->set_script(script);
		instance = base;
	}
	if (instance.is_null()) { instance.instantiate(); }

	instance->equipment_id = equipment_id;
	instance->definition = definition;
	instance->manager_id = ObjectID(get_instance_id());
	if (Node* parent = get_parent()) { instance->pawn_root_id = ObjectID(parent->get_instance_id()); }
	return instance;
}

// --- Server ---

void EquipmentManagerComponent::grant_server_side(const Ref<EquipmentInstance>& instance)
{
	AbilitySystemComponent* asc = get_ability_system_component();
	const Ref<EquipmentDefinition> definition = instance->definition;
	if (asc == nullptr || definition.is_null()) { return; }

	const Array abilities = definition->get_abilities_to_grant();
	for (int i = 0; i < abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = abilities[i];
		if (ability.is_null()) { continue; }
		const int64_t handle = asc->give_ability(ability, instance->instigator);
		if (handle != 0) { instance->granted_ability_handles.push_back(handle); }
	}

	const Array effects = definition->get_effects_to_apply();
	for (int i = 0; i < effects.size(); i++)
	{
		Ref<GameplayEffect> effect = effects[i];
		if (effect.is_null()) { continue; }
		const int64_t active_id = asc->apply_gameplay_effect_to_self(effect);
		if (active_id > 0) { instance->applied_effect_ids.push_back(active_id); }
	}

	const PackedStringArray tags = definition->get_granted_tags();
	for (int i = 0; i < tags.size(); i++)
	{
		asc->add_loose_gameplay_tag(GameplayTagsManager::get_singleton()->request_tag(tags[i]));
	}
}

void EquipmentManagerComponent::revoke_server_side(const Ref<EquipmentInstance>& instance)
{
	AbilitySystemComponent* asc = get_ability_system_component();
	const Ref<EquipmentDefinition> definition = instance->definition;
	if (asc == nullptr || definition.is_null()) { return; }

	for (int i = 0; i < instance->granted_ability_handles.size(); i++)
	{
		Ref<GameplayAbility> ability = asc->find_ability_by_handle(instance->granted_ability_handles[i]);
		if (ability.is_valid()) { asc->clear_ability(ability); }
	}
	instance->granted_ability_handles.clear();

	for (int i = 0; i < instance->applied_effect_ids.size(); i++)
	{
		asc->remove_active_gameplay_effect(instance->applied_effect_ids[i]);
	}
	instance->applied_effect_ids.clear();

	const PackedStringArray tags = definition->get_granted_tags();
	for (int i = 0; i < tags.size(); i++)
	{
		asc->remove_loose_gameplay_tag(GameplayTagsManager::get_singleton()->request_tag(tags[i]));
	}
}

Ref<EquipmentInstance> EquipmentManagerComponent::equip_item(const Ref<EquipmentDefinition>& definition, const Variant& instigator)
{
	if (refuse_on_mirror("equip_item")) { return Ref<EquipmentInstance>(); }
	ERR_FAIL_COND_V_MSG(definition.is_null(), Ref<EquipmentInstance>(), "GFGD: equip_item needs an EquipmentDefinition.");

	Ref<EquipmentInstance> instance = create_instance(definition, next_equipment_id++);
	instance->instigator = instigator;
	Ref<InventoryItemInstance> item = instigator;
	if (item.is_valid())
	{
		instance->item_id = item->get_item_id();
		instance->item_definition = item->get_item_definition();
	}
	instances.push_back(instance);

	grant_server_side(instance);
	instance->spawn_equipment_actors();
	instance->on_equipped();
	emit_signal("item_equipped", instance);

	if (is_sending_server())
	{
		if (ComponentReplication::loadable_path(definition).is_empty())
		{
			WARN_PRINT("GFGD: an EquipmentDefinition that is not a saved resource cannot reach clients; save it as a .tres.");
		}
		send_to_audience("client_equipped", serialize_instance(instance));
	}
	return instance;
}

void EquipmentManagerComponent::remove_instance_at(int index, bool server_side)
{
	Ref<EquipmentInstance> instance = instances[index];
	instances.remove_at(index);
	instance->on_unequipped();
	instance->destroy_equipment_actors();
	if (server_side) { revoke_server_side(instance); }
	emit_signal("item_unequipped", instance);
}

void EquipmentManagerComponent::unequip_item(const Ref<EquipmentInstance>& instance)
{
	if (refuse_on_mirror("unequip_item") || instance.is_null()) { return; }
	const int index = instances.find(instance);
	if (index < 0) { return; }
	const int64_t equipment_id = instance->equipment_id;
	remove_instance_at(index, true);
	if (is_sending_server()) { send_to_audience("client_unequipped", equipment_id); }
}

void EquipmentManagerComponent::unequip_all()
{
	if (refuse_on_mirror("unequip_all")) { return; }
	while (!instances.is_empty()) { unequip_item(instances[instances.size() - 1]); }
}

void EquipmentManagerComponent::send_to_audience(const StringName& method, const Variant& argument)
{
	const PackedInt32Array peers = ComponentReplication::resolve_peers(this, audience_of(replication_mode), subscribers);
	for (int i = 0; i < peers.size(); i++)
	{
		rpc_id(peers[i], method, argument);
	}
}

Dictionary EquipmentManagerComponent::serialize_instance(const Ref<EquipmentInstance>& instance) const
{
	Dictionary state;
	state["id"] = instance->equipment_id;
	state["def"] = ComponentReplication::loadable_path(instance->definition);
	state["item_id"] = instance->item_id;
	state["item_def"] = ComponentReplication::loadable_path(instance->item_definition);
	state["visible"] = instance->spawned_visible;
	return state;
}

void EquipmentManagerComponent::server_request_full_state()
{
	const ComponentReplication::Audience audience = audience_of(replication_mode);
	if (!ComponentReplication::is_sending_server(this, audience)) { return; }
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	const int sender = multiplayer.is_valid() ? multiplayer->get_remote_sender_id() : 0;
	if (sender <= 0) { return; }
	if (audience == ComponentReplication::AUDIENCE_OWNER && sender != ComponentReplication::get_owner_peer(this)) { return; }

	Array state;
	for (int i = 0; i < instances.size(); i++) { state.push_back(serialize_instance(instances[i])); }
	subscribers.insert(sender);
	rpc_id(sender, "client_receive_full_state", state);
}

// --- Client ---

void EquipmentManagerComponent::client_equipped(const Dictionary& state)
{
	const int64_t equipment_id = state.get("id", 0);
	if (equipment_id == 0) { return; }
	for (int i = 0; i < instances.size(); i++)
	{
		if (instances[i]->equipment_id == equipment_id) { return; }
	}

	const String path = state.get("def", String());
	Ref<EquipmentDefinition> definition = path.is_empty() ? Ref<EquipmentDefinition>() : Ref<EquipmentDefinition>(ResourceLoader::get_singleton()->load(path));
	Ref<EquipmentInstance> instance = create_instance(definition, equipment_id);
	instance->item_id = state.get("item_id", 0);
	const String item_path = state.get("item_def", String());
	if (!item_path.is_empty()) { instance->item_definition = ResourceLoader::get_singleton()->load(item_path); }
	Ref<InventoryItemInstance> item = InventoryManagerComponent::find_item_anywhere(this, instance->item_id);
	if (item.is_valid()) { instance->instigator = item; }
	instance->spawned_visible = state.get("visible", true);
	instances.push_back(instance);

	instance->spawn_equipment_actors();
	instance->on_equipped();
	emit_signal("item_equipped", instance);
}

void EquipmentManagerComponent::client_unequipped(int64_t equipment_id)
{
	for (int i = 0; i < instances.size(); i++)
	{
		if (instances[i]->equipment_id == equipment_id)
		{
			remove_instance_at(i, false);
			return;
		}
	}
}

void EquipmentManagerComponent::client_receive_full_state(const Array& state)
{
	HashSet<int64_t> kept;
	for (int i = 0; i < state.size(); i++)
	{
		const Dictionary entry = state[i];
		kept.insert(int64_t(entry.get("id", 0)));
	}
	for (int i = instances.size() - 1; i >= 0; i--)
	{
		if (!kept.has(instances[i]->equipment_id)) { remove_instance_at(i, false); }
	}
	for (int i = 0; i < state.size(); i++)
	{
		client_equipped(state[i]);
	}
	emit_signal("replicated_state_received");
}

// --- Reading ---

Array EquipmentManagerComponent::get_equipment_instances() const
{
	Array result;
	for (int i = 0; i < instances.size(); i++) { result.push_back(instances[i]); }
	return result;
}

Ref<EquipmentInstance> EquipmentManagerComponent::get_first_instance_of_type(const Variant& instance_class) const
{
	for (int i = 0; i < instances.size(); i++)
	{
		if (InventoryItemDefinition::object_is_of_class(instances[i].ptr(), instance_class)) { return instances[i]; }
	}
	return Ref<EquipmentInstance>();
}

Ref<EquipmentInstance> EquipmentManagerComponent::find_instance_by_definition(const Ref<EquipmentDefinition>& definition) const
{
	for (int i = 0; i < instances.size(); i++)
	{
		if (instances[i]->definition == definition) { return instances[i]; }
	}
	return Ref<EquipmentInstance>();
}

Ref<EquipmentInstance> EquipmentManagerComponent::find_instance_by_item_id(int64_t item_id) const
{
	if (item_id == 0) { return Ref<EquipmentInstance>(); }
	for (int i = 0; i < instances.size(); i++)
	{
		if (instances[i]->item_id == item_id) { return instances[i]; }
	}
	return Ref<EquipmentInstance>();
}

void EquipmentManagerComponent::_bind_methods()
{
	BIND_ENUM_CONSTANT(REPLICATION_NONE);
	BIND_ENUM_CONSTANT(REPLICATION_OWNER);
	BIND_ENUM_CONSTANT(REPLICATION_EVERYONE);

	ClassDB::bind_method(D_METHOD("is_mirror"), &EquipmentManagerComponent::is_mirror);
	ClassDB::bind_method(D_METHOD("get_ability_system_component"), &EquipmentManagerComponent::get_ability_system_component);
	ClassDB::bind_method(D_METHOD("equip_item", "definition", "instigator"), &EquipmentManagerComponent::equip_item, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("unequip_item", "instance"), &EquipmentManagerComponent::unequip_item);
	ClassDB::bind_method(D_METHOD("unequip_all"), &EquipmentManagerComponent::unequip_all);
	ClassDB::bind_method(D_METHOD("get_equipment_instances"), &EquipmentManagerComponent::get_equipment_instances);
	ClassDB::bind_method(D_METHOD("get_first_instance_of_type", "instance_class"), &EquipmentManagerComponent::get_first_instance_of_type);
	ClassDB::bind_method(D_METHOD("find_instance_by_definition", "definition"), &EquipmentManagerComponent::find_instance_by_definition);
	ClassDB::bind_method(D_METHOD("find_instance_by_item_id", "item_id"), &EquipmentManagerComponent::find_instance_by_item_id);
	ClassDB::bind_method(D_METHOD("set_replication_mode", "value"), &EquipmentManagerComponent::set_replication_mode);
	ClassDB::bind_method(D_METHOD("get_replication_mode"), &EquipmentManagerComponent::get_replication_mode);

	ClassDB::bind_method(D_METHOD("server_request_full_state"), &EquipmentManagerComponent::server_request_full_state);
	ClassDB::bind_method(D_METHOD("client_receive_full_state", "state"), &EquipmentManagerComponent::client_receive_full_state);
	ClassDB::bind_method(D_METHOD("client_equipped", "state"), &EquipmentManagerComponent::client_equipped);
	ClassDB::bind_method(D_METHOD("client_unequipped", "equipment_id"), &EquipmentManagerComponent::client_unequipped);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "replication_mode", PROPERTY_HINT_ENUM, "None,Owner,Everyone"), "set_replication_mode", "get_replication_mode");

	ADD_SIGNAL(MethodInfo("item_equipped", PropertyInfo(Variant::OBJECT, "instance", PROPERTY_HINT_RESOURCE_TYPE, "EquipmentInstance")));
	ADD_SIGNAL(MethodInfo("item_unequipped", PropertyInfo(Variant::OBJECT, "instance", PROPERTY_HINT_RESOURCE_TYPE, "EquipmentInstance")));
	ADD_SIGNAL(MethodInfo("replicated_state_received"));
}
}
