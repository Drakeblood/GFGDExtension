#include "ability_system/ability_system_component.h"
#include "ability_system/ability_async.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

#include "ability_system/gameplay_ability.h"
#include "gameplay_tags/gameplay_tag_count_container.h"
#include "gameplay_tags/gameplay_tag_container.h"
#include "gameplay_tags/gameplay_tag.h"
#include "ability_system/gameplay_effect.h"
#include "ability_system/gameplay_effect_spec.h"
#include "ability_system/active_gameplay_effect.h"
#include "ability_system/attribute_set.h"
#include "ability_system/attribute_modifier.h"
#include "ability_system/gameplay_cue.h"
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include "framework/net_driver.h"
#include "framework/pawn.h"
#include "framework/world.h"

using namespace godot;

namespace GFGD
{
namespace
{
Ref<GameplayTag> make_tag(const StringName& tag_name)
{
	Ref<GameplayTag> tag;
	tag.instantiate();
	tag->set_tag_name(tag_name);
	return tag;
}

PackedStringArray tag_names_of(const Ref<GameplayTagContainer>& container)
{
	return container.is_valid() ? container->get_tags() : PackedStringArray();
}

Ref<GameplayTagContainer> container_of(const PackedStringArray& names)
{
	if (names.is_empty()) { return Ref<GameplayTagContainer>(); }

	Ref<GameplayTagContainer> container;
	container.instantiate();
	container->set_tags(names);
	return container;
}

// A resource path a client can load by itself. Resources built in code have
// none, and a sub-resource's ("res://a.tres::Effect_1") is not loadable on its
// own - both are rebuilt from the data sent instead.
String loadable_path(const Ref<Resource>& resource)
{
	if (resource.is_null()) { return String(); }

	const String path = resource->get_path();
	if (path.is_empty() || path.contains("::")) { return String(); }
	return path;
}
}

AbilitySystemComponent::AbilitySystemComponent()
{
	gameplay_tag_count_container = memnew(GameplayTagCountContainer);
	blocked_ability_tags = memnew(GameplayTagCountContainer);
	next_active_effect_id = 1;
	next_ability_handle = 1;
	next_prediction_key = 1;
	replication_mode = REPLICATION_FULL;
	flush_queued = false;
	activating_for_client = false;
	replication_state_received = false;
	applying_remote = false;
}

AbilitySystemComponent::~AbilitySystemComponent()
{
	memdelete(gameplay_tag_count_container);
	memdelete(blocked_ability_tags);
}

void AbilitySystemComponent::register_async(AbilityAsync* async)
{
	if (async != nullptr && !asyncs.has(async))
	{
		asyncs.append(async);
	}
}

void AbilitySystemComponent::unregister_async(AbilityAsync* async)
{
	asyncs.erase(async);
}

void AbilitySystemComponent::_notification(int p_what)
{
	if (p_what != NOTIFICATION_PREDELETE) { return; }

	// Ended while this component still exists, so they can take their hooks
	// down; whatever awaits them is released.
	const Array ending = asyncs.duplicate();
	for (int i = 0; i < ending.size(); i++)
	{
		Ref<AbilityAsync> async = ending[i];
		if (async.is_valid())
		{
			async->end_action();
		}
	}
	asyncs.clear();
}

void AbilitySystemComponent::_ready()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	configure_rpcs();

	if (Pawn* pawn = Pawn::find_in(get_parent()))
	{
		pawn->set_abilities_replicated(replication_mode != REPLICATION_NONE);
	}

	if (attribute_set.is_valid())
	{
		runtime_attribute_set = attribute_set->duplicate(true);
		runtime_attribute_set->init_current_from_base();
		runtime_attribute_set->connect("attribute_changed", callable_mp(this, &AbilitySystemComponent::_on_attribute_set_changed));
		runtime_attribute_set->connect("base_value_changed", callable_mp(this, &AbilitySystemComponent::_on_attribute_base_changed));
	}

	set_physics_process(true);

	if (is_replicated_client())
	{
		// The server grants the abilities and applies the effects; a client that
		// did the same would count every startup buff twice. What it has until
		// the server's answer arrives is the template's attribute values.
		rpc_id(World::SERVER_PEER_ID, "server_request_full_state");
		return;
	}

	for (int i = 0; i < startup_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = startup_abilities[i];
		if (ability.is_valid())
		{
			give_ability(ability);
		}
	}

	for (int i = 0; i < startup_effects.size(); i++)
	{
		Ref<GameplayEffect> effect = startup_effects[i];
		if (effect.is_valid())
		{
			apply_gameplay_effect_to_self(effect);
		}
	}
}

void AbilitySystemComponent::_physics_process(double delta)
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	// Tasks tick wherever abilities run - a predicting client's included.
	const Array abilities = activatable_abilities;
	for (int i = 0; i < abilities.size(); i++)
	{
		const Ref<GameplayAbility> ability = abilities[i];
		if (ability.is_valid() && ability->get_is_active())
		{
			ability->tick_tasks(delta);
		}
	}

	if (is_replicated_client())
	{
		// Mirrors only count down, so a HUD can draw the time left. They end when
		// the server says so, not when this clock says so.
		for (int i = 0; i < active_effects.size(); i++)
		{
			Ref<ActiveGameplayEffect> active = active_effects[i];
			if (active->effect.is_valid() && active->effect->get_duration_policy() == GameplayEffect::HAS_DURATION)
			{
				active->remaining_time = MAX(0.0, active->remaining_time - delta);
			}
		}
		return;
	}

	bool attributes_dirty = false;
	for (int i = active_effects.size() - 1; i >= 0; i--)
	{
		Ref<ActiveGameplayEffect> active = active_effects[i];
		Ref<GameplayEffect> effect = active->effect;
		if (effect.is_null()) { continue; }

		if (effect->get_period() > 0.0f)
		{
			active->period_accumulator += delta;
			while (active->period_accumulator >= effect->get_period())
			{
				active->period_accumulator -= effect->get_period();
				execute_periodic(active);
				attributes_dirty = true;
			}
		}

		if (effect->get_duration_policy() == GameplayEffect::HAS_DURATION)
		{
			active->remaining_time -= delta;
			if (active->remaining_time <= 0.0)
			{
				if (effect->get_stack_expiration() == GameplayEffect::REMOVE_SINGLE_AND_REFRESH && active->stack_count > 1)
				{
					active->stack_count--;
					active->remaining_time = active->duration;
					emit_signal("gameplay_effect_stack_changed", effect, active->active_id, active->stack_count);
					queue_call(TARGET_EFFECT_VIEWERS, "client_effect_updated", Array::make(active->active_id, active->duration, active->remaining_time, active->stack_count));
				}
				else
				{
					remove_active_effect_internal(i);
				}
				attributes_dirty = true;
			}
		}
	}

	if (attributes_dirty)
	{
		recompute_all_attributes();
	}
}

// --- Networking basics ---

World* AbilitySystemComponent::get_world_node() const
{
	return is_inside_tree() ? Object::cast_to<World>(get_tree()) : nullptr;
}

bool AbilitySystemComponent::has_authority() const
{
	return !is_replicated_client();
}

bool AbilitySystemComponent::is_replicated_client() const
{
	if (replication_mode == REPLICATION_NONE) { return false; }

	World* world = get_world_node();
	return world != nullptr && world->is_networked() && !world->has_authority();
}

bool AbilitySystemComponent::is_replicating_server() const
{
	if (replication_mode == REPLICATION_NONE) { return false; }

	World* world = get_world_node();
	return world != nullptr && world->is_networked() && world->has_authority();
}

int AbilitySystemComponent::get_owner_peer_id() const
{
	World* world = get_world_node();
	if (world == nullptr) { return World::SERVER_PEER_ID; }

	// Asked of the parent: a component beside a Pawn belongs to whoever owns the
	// pawn, and the lookup finds the Pawn one step below the node it is given.
	Node* parent = get_parent();
	return world->get_net_owner_peer(parent != nullptr ? parent : const_cast<AbilitySystemComponent*>(this));
}

bool AbilitySystemComponent::owner_is_remote() const
{
	World* world = get_world_node();
	if (world == nullptr) { return false; }

	const int owner = get_owner_peer_id();
	return owner != World::SERVER_PEER_ID && owner != world->get_local_peer_id();
}

bool AbilitySystemComponent::runs_ability_logic(const Ref<GameplayAbility>& ability) const
{
	if (ability.is_null()) { return false; }
	return !(is_replicated_client() && ability->get_net_execution_policy() == GameplayAbility::SERVER_ONLY);
}

bool AbilitySystemComponent::is_sender_owner() const
{
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	const int sender = multiplayer.is_valid() ? multiplayer->get_remote_sender_id() : 0;
	if (sender != get_owner_peer_id())
	{
		WARN_PRINT(vformat("GFGD: peer %d sent an ability request to %s, which peer %d owns; ignoring it.", sender, String(get_path()), get_owner_peer_id()));
		return false;
	}
	return true;
}

void AbilitySystemComponent::configure_rpcs()
{
	// Reliable and on channel 0, the channel the NetDriver spawns nodes on:
	// everything here is state, and state that arrives before the node it is
	// for, or out of order, is wrong rather than late.
	Dictionary from_anyone;
	from_anyone["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
	from_anyone["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
	from_anyone["call_local"] = false;
	from_anyone["channel"] = 0;

	Dictionary from_server = from_anyone.duplicate();
	from_server["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;

	rpc_config("server_request_full_state", from_anyone);
	rpc_config("server_try_activate_ability", from_anyone);
	rpc_config("server_end_ability", from_anyone);
	rpc_config("server_ability_input", from_anyone);
	rpc_config("server_ability_target_data", from_anyone);

	rpc_config("client_receive_full_state", from_server);
	rpc_config("client_update_attributes", from_server);
	rpc_config("client_update_tags", from_server);
	rpc_config("client_effect_added", from_server);
	rpc_config("client_effect_updated", from_server);
	rpc_config("client_effect_removed", from_server);
	rpc_config("client_ability_given", from_server);
	rpc_config("client_ability_removed", from_server);
	rpc_config("client_ability_activated", from_server);
	rpc_config("client_ability_ended", from_server);
	rpc_config("client_activation_confirmed", from_server);
	rpc_config("client_activation_rejected", from_server);
	rpc_config("client_gameplay_cue", from_server);
}

// --- Abilities ---

int64_t AbilitySystemComponent::give_ability(const Ref<GameplayAbility>& ability_template, const Variant& source_object, float level)
{
	if (ability_template.is_null()) { return 0; }

	if (is_replicated_client())
	{
		WARN_PRINT("GFGD: give_ability was called on a client; abilities are granted by the server and arrive from it. Ignoring it.");
		return 0;
	}

	Ref<GameplayAbility> ability = ability_template->duplicate();
	ability->set_handle(next_ability_handle++);
	ability->set_ability_level(level);
	activatable_abilities.append(ability);

	// The template's own path, not the copy's - the copy has none, and the
	// client builds its own copy from the same template.
	ability_template_paths[ability->get_handle()] = loadable_path(ability_template);

	ability->setup_ability(this, source_object);
	ability->on_give_ability();
	emit_signal("ability_given", ability);

	queue_call(TARGET_OWNER, "client_ability_given", Array::make(serialize_ability(ability)));

	return ability->get_handle();
}

void AbilitySystemComponent::clear_ability(const Ref<GameplayAbility>& ability)
{
	if (ability.is_null()) { return; }

	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> current_ability = activatable_abilities[i];
		if (current_ability == ability)
		{
			if (current_ability->get_is_active())
			{
				current_ability->connect("ability_ended",
						callable_mp(this, &AbilitySystemComponent::_on_ability_ended_clear).bind(ability),
						CONNECT_ONE_SHOT);
				return;
			}

			activatable_abilities.remove_at(i);
			ability_template_paths.erase(current_ability->get_handle());
			current_ability->on_remove_ability();
			queue_call(TARGET_OWNER, "client_ability_removed", Array::make(current_ability->get_handle()));
			return;
		}
	}
}

void AbilitySystemComponent::clear_all_abilities()
{
	const Array abilities = activatable_abilities.duplicate();
	for (int i = 0; i < abilities.size(); i++)
	{
		const Ref<GameplayAbility> ability = abilities[i];
		if (ability.is_valid() && ability->get_is_active())
		{
			ability->end_ability(true);
		}
		clear_ability(ability);
	}
}

void AbilitySystemComponent::_on_ability_ended_clear(bool was_canceled, const Ref<GameplayAbility>& ability)
{
	activatable_abilities.erase(ability);
	ability_template_paths.erase(ability->get_handle());
	ability->on_remove_ability();
	queue_call(TARGET_OWNER, "client_ability_removed", Array::make(ability->get_handle()));
}

Ref<GameplayAbility> AbilitySystemComponent::find_ability_by_name(const StringName& ability_name) const
{
	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = activatable_abilities[i];
		if (ability.is_valid() && ability->get_ability_name() == ability_name) { return ability; }
	}
	return Ref<GameplayAbility>();
}

Ref<GameplayAbility> AbilitySystemComponent::find_ability_by_handle(int64_t handle) const
{
	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = activatable_abilities[i];
		if (ability.is_valid() && ability->get_handle() == handle) { return ability; }
	}
	return Ref<GameplayAbility>();
}

bool AbilitySystemComponent::try_activate_ability(const StringName& ability_name)
{
	const Ref<GameplayAbility> ability = find_ability_by_name(ability_name);
	return ability.is_valid() && try_activate_ability_internal(ability);
}

bool AbilitySystemComponent::try_activate_ability_by_handle(int64_t handle)
{
	const Ref<GameplayAbility> ability = find_ability_by_handle(handle);
	return ability.is_valid() && try_activate_ability_internal(ability);
}

bool AbilitySystemComponent::try_activate_ability_internal(const Ref<GameplayAbility>& ability, const StringName& event_tag, const Dictionary& event_data)
{
	// Set before the checks, so a script's _can_activate_ability can read the
	// event too - and cleared by an activation nothing triggered.
	ability->set_trigger_event(event_tag, event_data);

	if (!ability->can_activate_ability())
	{
		emit_signal("ability_activation_failed", ability, ability->get_last_failure_reason());
		return false;
	}

	if (!is_replicated_client())
	{
		// Started here, not asked for by a client: nothing to wait to hear from.
		ability->set_prediction_key(0);
		ability->activate_ability();
		return true;
	}

	switch (ability->get_net_execution_policy())
	{
		case GameplayAbility::LOCAL_ONLY:
			ability->activate_ability();
			return true;

		case GameplayAbility::SERVER_ONLY:
			// Only asked for. It becomes active here when the server says it is.
			rpc_id(World::SERVER_PEER_ID, "server_try_activate_ability", ability->get_handle(), 0, event_tag, event_data);
			return true;

		case GameplayAbility::LOCAL_PREDICTED:
		default:
		{
			// The request goes first, so if the ability ends inside its own
			// activation the server hears "start" before "stop".
			const int64_t key = next_prediction_key++;
			ability->set_predicting(true);
			ability->set_prediction_key(key);
			rpc_id(World::SERVER_PEER_ID, "server_try_activate_ability", ability->get_handle(), key, event_tag, event_data);
			ability->activate_ability();
			return true;
		}
	}
}

void AbilitySystemComponent::cancel_abilities_with_tags(const Ref<GameplayTagContainer>& tag_container)
{
	if (tag_container.is_null() || tag_container->get_length() == 0) { return; }

	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = activatable_abilities[i];
		if (ability.is_null() || !ability->get_is_active()) { continue; }

		if (ability->get_ability_tags().is_valid() && ability->get_ability_tags()->has_any(tag_container))
		{
			ability->end_ability(true);
		}
	}
}

void AbilitySystemComponent::cancel_all_abilities()
{
	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = activatable_abilities[i];
		if (ability.is_valid() && ability->get_is_active())
		{
			ability->end_ability(true);
		}
	}
}

void AbilitySystemComponent::ability_local_input_pressed(const StringName& action_name)
{
	// A remote player's buttons reach the server twice: as the action state the
	// controller replicates, which fires the pawn's bindings here, and as this
	// component's own requests from the client. Only the second is listened to,
	// or every press would activate the ability twice.
	if (is_replicating_server() && owner_is_remote()) { return; }

	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = activatable_abilities[i];
		if (ability.is_valid() && ability->get_input_action_name() == action_name)
		{
			ability->set_is_input_pressed(true);

			if (ability->get_is_active())
			{
				if (runs_ability_logic(ability))
				{
					ability->input_pressed();
				}
				if (is_replicated_client() && ability->get_net_execution_policy() != GameplayAbility::LOCAL_ONLY)
				{
					rpc_id(World::SERVER_PEER_ID, "server_ability_input", ability->get_handle(), true);
				}
			}
			else
			{
				try_activate_ability_internal(ability);
			}
		}
	}
}

void AbilitySystemComponent::ability_local_input_released(const StringName& action_name)
{
	if (is_replicating_server() && owner_is_remote()) { return; }

	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = activatable_abilities[i];
		if (ability.is_valid() && ability->get_input_action_name() == action_name)
		{
			ability->set_is_input_pressed(false);

			if (ability->get_is_active())
			{
				if (runs_ability_logic(ability))
				{
					ability->input_released();
				}
				if (is_replicated_client() && ability->get_net_execution_policy() != GameplayAbility::LOCAL_ONLY)
				{
					rpc_id(World::SERVER_PEER_ID, "server_ability_input", ability->get_handle(), false);
				}
			}
		}
	}
}

void AbilitySystemComponent::notify_ability_activated(const Ref<GameplayAbility>& ability)
{
	emit_signal("ability_activated", ability);

	// Tell the owner about an activation it did not ask for - the server
	// starting it on its own, or a SERVER_ONLY one the owner is waiting on. A
	// predicted activation the owner asked for, it already ran.
	if (!is_replicating_server() || !owner_is_remote()) { return; }
	if (ability->get_net_execution_policy() == GameplayAbility::LOCAL_ONLY) { return; }
	if (activating_for_client && ability->get_net_execution_policy() == GameplayAbility::LOCAL_PREDICTED) { return; }

	queue_call(TARGET_OWNER, "client_ability_activated", Array::make(ability->get_handle()));
}

void AbilitySystemComponent::notify_ability_ended(const Ref<GameplayAbility>& ability, bool was_canceled)
{
	emit_signal("ability_ended", ability, was_canceled);

	if (applying_remote) { return; }
	if (ability->get_net_execution_policy() == GameplayAbility::LOCAL_ONLY) { return; }

	if (is_replicating_server() && owner_is_remote())
	{
		queue_call(TARGET_OWNER, "client_ability_ended", Array::make(ability->get_handle(), was_canceled));
	}
	else if (is_replicated_client())
	{
		rpc_id(World::SERVER_PEER_ID, "server_end_ability", ability->get_handle(), was_canceled);
	}
}

// --- Tags ---

Ref<GameplayTagContainer> AbilitySystemComponent::get_owned_gameplay_tags() const
{
	return gameplay_tag_count_container->get_explicit_tags();
}

Ref<GameplayTagContainer> AbilitySystemComponent::get_blocked_ability_tags() const
{
	return blocked_ability_tags->get_explicit_tags();
}

void AbilitySystemComponent::update_tag_map(const Ref<GameplayTag>& tag, int count_delta)
{
	if (tag.is_null() || count_delta == 0) { return; }

	if (gameplay_tag_count_container->update_tag_count(tag, count_delta))
	{
		on_tag_updated(tag, count_delta > 0);
	}

	const StringName tag_name = tag->get_tag_name();
	emit_signal("owned_tag_changed", tag_name, gameplay_tag_count_container->get_tag_count(tag_name));

	if (is_replicating_server())
	{
		dirty_tags[tag_name] = true;
		queue_flush();
	}
}

void AbilitySystemComponent::register_gameplay_tag_event(const Ref<GameplayTag>& tag, const Callable& tag_delegate)
{
	gameplay_tag_count_container->register_gameplay_tag_event(tag, tag_delegate);
}

void AbilitySystemComponent::update_blocked_ability_tags(const Ref<GameplayTag>& tag, int count_delta)
{
	blocked_ability_tags->update_tag_count(tag, count_delta);
}

bool AbilitySystemComponent::has_matching_gameplay_tag(const Ref<GameplayTag>& tag) const
{
	return tag.is_valid() && get_owned_gameplay_tags()->has_tag(tag);
}

bool AbilitySystemComponent::has_any_matching_gameplay_tags(const Ref<GameplayTagContainer>& tag_container) const
{
	return get_owned_gameplay_tags()->has_any(tag_container);
}

bool AbilitySystemComponent::has_all_matching_gameplay_tags(const Ref<GameplayTagContainer>& tag_container) const
{
	return get_owned_gameplay_tags()->has_all(tag_container);
}

int AbilitySystemComponent::get_gameplay_tag_count(const Ref<GameplayTag>& tag) const
{
	return tag.is_valid() ? gameplay_tag_count_container->get_tag_count(tag->get_tag_name()) : 0;
}

void AbilitySystemComponent::add_loose_gameplay_tag(const Ref<GameplayTag>& tag, int count)
{
	if (count > 0) { update_tag_map(tag, count); }
}

void AbilitySystemComponent::remove_loose_gameplay_tag(const Ref<GameplayTag>& tag, int count)
{
	if (count > 0) { update_tag_map(tag, -count); }
}

void AbilitySystemComponent::on_tag_updated(const Ref<GameplayTag>& tag, bool tag_exists)
{

}

// --- Effects & attributes ---

Ref<GameplayEffectSpec> AbilitySystemComponent::make_outgoing_spec(const Ref<GameplayEffect>& effect, float level)
{
	Ref<GameplayEffectSpec> spec;
	spec.instantiate();
	spec->set_effect(effect);
	spec->set_level(level);
	spec->set_source(this);
	return spec;
}

int64_t AbilitySystemComponent::apply_gameplay_effect_to_self(const Ref<GameplayEffect>& effect, float level)
{
	if (effect.is_null()) { return -1; }
	return apply_gameplay_effect_spec_to_self(make_outgoing_spec(effect, level));
}

int64_t AbilitySystemComponent::apply_gameplay_effect_to_target(const Ref<GameplayEffect>& effect, AbilitySystemComponent* target, float level)
{
	if (effect.is_null() || target == nullptr) { return -1; }
	return target->apply_gameplay_effect_spec_to_self(make_outgoing_spec(effect, level));
}

int64_t AbilitySystemComponent::apply_gameplay_effect_spec_to_target(const Ref<GameplayEffectSpec>& spec, AbilitySystemComponent* target)
{
	if (spec.is_null() || target == nullptr) { return -1; }
	if (spec->get_source() == nullptr)
	{
		spec->set_source(this);
	}
	return target->apply_gameplay_effect_spec_to_self(spec);
}

int64_t AbilitySystemComponent::apply_gameplay_effect_spec_to_self(const Ref<GameplayEffectSpec>& spec)
{
	if (spec.is_null() || spec->get_effect().is_null()) { return -1; }

	if (!can_apply_effects())
	{
		WARN_PRINT_ONCE("GFGD: a gameplay effect was applied on a client whose AbilitySystemComponent is replicated. Effects are applied on the server and their results arrive from it; this one was ignored.");
		return -1;
	}

	const Ref<GameplayEffect> effect = spec->get_effect();

	Ref<GameplayTagContainer> owned_tags = get_owned_gameplay_tags();
	if (effect->get_application_blocked_tags().is_valid() && effect->get_application_blocked_tags()->get_length() > 0
			&& owned_tags->has_any(effect->get_application_blocked_tags()))
	{
		return -1;
	}
	if (!owned_tags->has_all(effect->get_application_required_tags()))
	{
		return -1;
	}

	if (effect->get_remove_effects_with_tags().is_valid() && effect->get_remove_effects_with_tags()->get_length() > 0)
	{
		remove_active_effects_with_tags(effect->get_remove_effects_with_tags());
	}

	if (effect->get_duration_policy() == GameplayEffect::INSTANT)
	{
		Ref<ActiveGameplayEffect> once;
		once.instantiate();
		once->effect = effect;
		once->spec = spec;
		calculate_magnitudes(once);

		apply_modifiers_to_base(once);
		recompute_all_attributes();
		fire_effect_cues(once, CUE_EXECUTED);
		emit_signal("gameplay_effect_applied", effect, 0);
		return 0;
	}

	// Aggregate stacking: the same effect again adds to the one running.
	if (effect->get_stacking_type() == GameplayEffect::STACKING_AGGREGATE)
	{
		for (int i = 0; i < active_effects.size(); i++)
		{
			Ref<ActiveGameplayEffect> existing = active_effects[i];
			if (existing->effect != effect) { continue; }

			const int limit = effect->get_stack_limit();
			if (limit <= 0 || existing->stack_count < limit)
			{
				existing->stack_count++;
			}
			if (effect->get_stack_duration_refresh())
			{
				existing->remaining_time = existing->duration;
			}

			recompute_all_attributes();
			emit_signal("gameplay_effect_stack_changed", effect, existing->active_id, existing->stack_count);
			queue_call(TARGET_EFFECT_VIEWERS, "client_effect_updated", Array::make(existing->active_id, existing->duration, existing->remaining_time, existing->stack_count));
			return existing->active_id;
		}
	}

	Ref<ActiveGameplayEffect> active;
	active.instantiate();
	active->effect = effect;
	active->spec = spec;
	active->active_id = next_active_effect_id++;
	active->duration = (effect->get_duration_policy() == GameplayEffect::HAS_DURATION) ? effect->get_duration() : 0.0;
	active->remaining_time = active->duration;
	calculate_magnitudes(active);
	active_effects.push_back(active);

	grant_effect_tags(effect, 1);
	fire_effect_cues(active, CUE_ADDED);

	if (effect->get_period() > 0.0f && effect->get_execute_period_on_application())
	{
		execute_periodic(active);
	}

	recompute_all_attributes();

	emit_signal("gameplay_effect_applied", effect, active->active_id);
	queue_call(TARGET_EFFECT_VIEWERS, "client_effect_added", Array::make(serialize_effect(active)));
	return active->active_id;
}

bool AbilitySystemComponent::remove_active_gameplay_effect(int64_t active_id, int stacks_to_remove)
{
	if (!can_apply_effects()) { return false; }

	const int index = find_active_effect_index(active_id);
	if (index < 0) { return false; }

	Ref<ActiveGameplayEffect> active = active_effects[index];
	if (stacks_to_remove > 0 && active->stack_count > stacks_to_remove)
	{
		active->stack_count -= stacks_to_remove;
		recompute_all_attributes();
		emit_signal("gameplay_effect_stack_changed", active->effect, active->active_id, active->stack_count);
		queue_call(TARGET_EFFECT_VIEWERS, "client_effect_updated", Array::make(active->active_id, active->duration, active->remaining_time, active->stack_count));
		return true;
	}

	remove_active_effect_internal(index);
	recompute_all_attributes();
	return true;
}

int AbilitySystemComponent::remove_active_effects_with_tags(const Ref<GameplayTagContainer>& tag_container)
{
	if (!can_apply_effects()) { return 0; }
	if (tag_container.is_null() || tag_container->get_length() == 0) { return 0; }

	int removed_count = 0;
	for (int i = active_effects.size() - 1; i >= 0; i--)
	{
		Ref<GameplayEffect> effect = active_effects[i]->effect;
		if (effect.is_valid() && effect->get_effect_tags().is_valid() && effect->get_effect_tags()->has_any(tag_container))
		{
			remove_active_effect_internal(i);
			removed_count++;
		}
	}

	if (removed_count > 0)
	{
		recompute_all_attributes();
	}
	return removed_count;
}

Array AbilitySystemComponent::get_active_effects() const
{
	Array result;
	for (int i = 0; i < active_effects.size(); i++)
	{
		result.append(active_effects[i]);
	}
	return result;
}

int AbilitySystemComponent::find_active_effect_index(int64_t active_id) const
{
	for (int i = 0; i < active_effects.size(); i++)
	{
		if (active_effects[i]->active_id == active_id) { return i; }
	}
	return -1;
}

Ref<ActiveGameplayEffect> AbilitySystemComponent::get_active_effect(int64_t active_id) const
{
	const int index = find_active_effect_index(active_id);
	return index >= 0 ? active_effects[index] : Ref<ActiveGameplayEffect>();
}

int AbilitySystemComponent::get_active_effect_stack_count(int64_t active_id) const
{
	const int index = find_active_effect_index(active_id);
	return index >= 0 ? active_effects[index]->stack_count : 0;
}

double AbilitySystemComponent::get_active_effect_remaining_time(int64_t active_id) const
{
	const int index = find_active_effect_index(active_id);
	return index >= 0 ? active_effects[index]->remaining_time : 0.0;
}

double AbilitySystemComponent::get_effects_time_remaining_with_granted_tags(const Ref<GameplayTagContainer>& tag_container) const
{
	if (tag_container.is_null() || tag_container->get_length() == 0) { return 0.0; }

	double longest = 0.0;
	for (int i = 0; i < active_effects.size(); i++)
	{
		const Ref<ActiveGameplayEffect>& active = active_effects[i];
		if (active->effect.is_null() || active->effect->get_duration_policy() != GameplayEffect::HAS_DURATION) { continue; }

		const Ref<GameplayTagContainer> granted = active->effect->get_granted_tags();
		if (granted.is_valid() && granted->has_any(tag_container))
		{
			longest = MAX(longest, active->remaining_time);
		}
	}
	return longest;
}

Ref<AttributeSet> AbilitySystemComponent::get_attribute_set() const
{
	return runtime_attribute_set.is_valid() ? runtime_attribute_set : attribute_set;
}

double AbilitySystemComponent::get_attribute_value(const StringName& attribute_name) const
{
	Ref<AttributeSet> set = get_attribute_set();
	return set.is_valid() ? set->get_current_value(attribute_name) : 0.0;
}

double AbilitySystemComponent::get_attribute_base_value(const StringName& attribute_name) const
{
	Ref<AttributeSet> set = get_attribute_set();
	return set.is_valid() ? set->get_base_value(attribute_name) : 0.0;
}

void AbilitySystemComponent::set_attribute_base_value(const StringName& attribute_name, double value)
{
	if (!can_apply_effects())
	{
		WARN_PRINT_ONCE("GFGD: set_attribute_base_value was called on a client whose AbilitySystemComponent is replicated; the server's value arrives on its own. Ignored.");
		return;
	}

	Ref<AttributeSet> set = get_attribute_set();
	if (set.is_valid())
	{
		set->set_base_value(attribute_name, value);
		recompute_all_attributes();
	}
}

void AbilitySystemComponent::_on_attribute_set_changed(const StringName& attribute_name, double old_value, double new_value)
{
	emit_signal("attribute_changed", attribute_name, old_value, new_value);

	if (is_replicating_server())
	{
		dirty_attributes[attribute_name] = true;
		queue_flush();
	}
}

void AbilitySystemComponent::_on_attribute_base_changed(const StringName& attribute_name, double old_value, double new_value)
{
	if (is_replicating_server())
	{
		dirty_attributes[attribute_name] = true;
		queue_flush();
	}
}

void AbilitySystemComponent::calculate_magnitudes(const Ref<ActiveGameplayEffect>& active)
{
	const TypedArray<AttributeModifier> modifiers = active->effect->get_modifiers();
	active->magnitudes.resize(modifiers.size());

	for (int i = 0; i < modifiers.size(); i++)
	{
		const Ref<AttributeModifier> modifier = modifiers[i];
		active->magnitudes.set(i, modifier.is_valid() ? modifier->calculate_magnitude(active->spec, this) : 0.0);
	}
}

void AbilitySystemComponent::apply_modifiers_to_base(const Ref<ActiveGameplayEffect>& active)
{
	Ref<AttributeSet> set = get_attribute_set();
	if (set.is_null()) { return; }

	TypedArray<AttributeModifier> modifiers = active->effect->get_modifiers();
	for (int i = 0; i < modifiers.size(); i++)
	{
		Ref<AttributeModifier> modifier = modifiers[i];
		if (modifier.is_null() || modifier->get_attribute() == StringName()) { continue; }

		const double magnitude = active->get_stacked_magnitude(i, modifier->get_operation());
		const double base_value = set->get_base_value(modifier->get_attribute());
		set->set_base_value(modifier->get_attribute(), modifier->apply_magnitude(base_value, magnitude));
	}
}

void AbilitySystemComponent::execute_periodic(const Ref<ActiveGameplayEffect>& active)
{
	apply_modifiers_to_base(active);
	fire_effect_cues(active, CUE_EXECUTED);
}

void AbilitySystemComponent::grant_effect_tags(const Ref<GameplayEffect>& effect, int direction)
{
	Ref<GameplayTagContainer> granted = effect->get_granted_tags();
	if (granted.is_null()) { return; }

	for (int i = 0; i < granted->get_length(); i++)
	{
		Ref<GameplayTag> tag = granted->get_tag(i);
		if (tag.is_valid())
		{
			update_tag_map(tag, direction);
		}
	}
}

void AbilitySystemComponent::fire_effect_cues(const Ref<ActiveGameplayEffect>& active, CueEvent event)
{
	const Ref<GameplayEffect> effect = active->effect;
	const Ref<GameplayTagContainer> cues = effect.is_valid() ? effect->get_gameplay_cue_tags() : Ref<GameplayTagContainer>();
	if (cues.is_null() || cues->get_length() == 0) { return; }

	const Ref<GameplayEffectSpec> spec = active->spec;

	Dictionary parameters;
	parameters["level"] = spec.is_valid() ? spec->get_level() : 1.0f;
	parameters["stack_count"] = active->stack_count;
	parameters["effect_path"] = loadable_path(effect);

	// What Unreal calls RawMagnitude: how strong this application is - the first
	// modifier's magnitude, stacks included. A hit spark scales with it.
	const TypedArray<AttributeModifier> modifiers = effect->get_modifiers();
	if (!modifiers.is_empty())
	{
		const Ref<AttributeModifier> first = modifiers[0];
		parameters["magnitude"] = first.is_valid() ? active->get_stacked_magnitude(0, first->get_operation()) : 0.0;
	}

	// Paths rather than nodes: a node cannot cross the wire, and the path is the
	// same on every peer.
	AbilitySystemComponent* source = spec.is_valid() ? spec->get_source() : nullptr;
	if (source != nullptr && source->is_inside_tree())
	{
		Node* avatar = source->get_parent() != nullptr ? source->get_parent() : source;
		parameters["source_path"] = avatar->get_path();
	}

	if (spec.is_valid())
	{
		const Dictionary context = spec->get_context();
		if (!context.is_empty())
		{
			parameters["context"] = context;
			// The two placement keys are lifted out, where a scene handler looks.
			if (context.has("location")) { parameters["location"] = context["location"]; }
			if (context.has("normal")) { parameters["normal"] = context["normal"]; }
		}
	}

	for (int i = 0; i < cues->get_length(); i++)
	{
		const Ref<GameplayTag> tag = cues->get_tag(i);
		if (tag.is_null()) { continue; }

		switch (event)
		{
			case CUE_ADDED:
				add_gameplay_cue(tag, parameters);
				break;
			case CUE_REMOVED:
				remove_gameplay_cue(tag);
				break;
			case CUE_EXECUTED:
			default:
				execute_gameplay_cue(tag, parameters);
				break;
		}
	}
}

void AbilitySystemComponent::remove_active_effect_internal(int index)
{
	Ref<ActiveGameplayEffect> active = active_effects[index];
	active_effects.remove_at(index);

	if (active->effect.is_valid())
	{
		grant_effect_tags(active->effect, -1);
		fire_effect_cues(active, CUE_REMOVED);
	}
	emit_signal("gameplay_effect_removed", active->effect, active->active_id);
	queue_call(TARGET_EFFECT_VIEWERS, "client_effect_removed", Array::make(active->active_id));
}

void AbilitySystemComponent::recompute_all_attributes()
{
	// A client's current values are the server's; its effect mirrors carry
	// modifiers too, and counting them again here would apply every buff twice.
	if (is_replicated_client()) { return; }

	Ref<AttributeSet> set = get_attribute_set();
	if (set.is_null()) { return; }

	PackedStringArray names = set->get_attribute_names();
	for (int n = 0; n < names.size(); n++)
	{
		StringName attribute_name = names[n];
		double base_value = set->get_base_value(attribute_name);
		double additive = 0.0;
		double multiplier = 1.0;
		bool has_override = false;
		double override_value = 0.0;

		for (int i = 0; i < active_effects.size(); i++)
		{
			const Ref<ActiveGameplayEffect>& active = active_effects[i];
			Ref<GameplayEffect> effect = active->effect;
			if (effect.is_null()) { continue; }
			// Periodic effects mutate base values on tick instead of aggregating.
			if (effect->get_period() > 0.0f) { continue; }

			TypedArray<AttributeModifier> modifiers = effect->get_modifiers();
			for (int m = 0; m < modifiers.size(); m++)
			{
				Ref<AttributeModifier> modifier = modifiers[m];
				if (modifier.is_null() || modifier->get_attribute() != attribute_name) { continue; }

				const double magnitude = active->get_stacked_magnitude(m, modifier->get_operation());
				switch (modifier->get_operation())
				{
					case AttributeModifier::ADD:
						additive += magnitude;
						break;
					case AttributeModifier::MULTIPLY:
						multiplier *= magnitude;
						break;
					case AttributeModifier::OVERRIDE:
						has_override = true;
						override_value = magnitude;
						break;
				}
			}
		}

		double current_value = has_override ? override_value : (base_value + additive) * multiplier;
		set->set_current_value(attribute_name, current_value);
	}
}

// --- Gameplay cues ---

void AbilitySystemComponent::emit_cue(const StringName& cue_tag_name, CueEvent event, const Dictionary& parameters)
{
	emit_signal("gameplay_cue", cue_tag_name, (int)event, parameters);
	run_cue_handler(cue_tag_name, event, parameters);
}

void AbilitySystemComponent::run_cue_handler(const StringName& cue_tag_name, CueEvent event, const Dictionary& parameters)
{
	// A dedicated server has nobody to show anything to - Unreal skips cues there
	// too. It still counts and sends them.
	World* world = get_world_node();
	if (world != nullptr && world->get_net_mode() == World::NET_MODE_DEDICATED_SERVER) { return; }

	const Ref<Resource> handler = GameplayCueManager::get_singleton()->find_handler(cue_tag_name);
	if (handler.is_null()) { return; }

	Node* target = get_parent() != nullptr ? get_parent() : this;

	if (Ref<GameplayCueNotify> notify = handler; notify.is_valid())
	{
		switch (event)
		{
			case CUE_ADDED: notify->on_active(target, parameters); break;
			case CUE_REMOVED: notify->on_removed(target, parameters); break;
			case CUE_EXECUTED:
			default: notify->execute(target, parameters); break;
		}
		return;
	}

	const Ref<PackedScene> scene = handler;
	if (scene.is_null())
	{
		WARN_PRINT_ONCE(vformat("GFGD: the handler for gameplay cue \"%s\" is neither a GameplayCueNotify nor a PackedScene.", String(cue_tag_name)));
		return;
	}

	if (event == CUE_REMOVED)
	{
		const ObjectID* id = active_cue_nodes.getptr(cue_tag_name);
		Node* node = id != nullptr ? Object::cast_to<Node>(ObjectDB::get_instance(*id)) : nullptr;
		active_cue_nodes.erase(cue_tag_name);
		if (node == nullptr) { return; }

		// A node that wants to fade out says so by having _on_removed, and frees
		// itself when done; anything else goes at once.
		if (node->has_method("_on_removed"))
		{
			node->call("_on_removed", parameters);
		}
		else
		{
			node->queue_free();
		}
		return;
	}

	const bool has_location = parameters.has("location");

	// A lasting effect with a period executes its cues on every tick, while the
	// same cue is already running under the character from when the effect was
	// added. That instance is the one to tell - Unreal hands an execute to the
	// existing cue actor too - rather than a new one per tick piling up for as
	// long as the effect lasts.
	if (event == CUE_EXECUTED && !has_location)
	{
		const ObjectID* id = active_cue_nodes.getptr(cue_tag_name);
		Node* active = id != nullptr ? Object::cast_to<Node>(ObjectDB::get_instance(*id)) : nullptr;
		if (active != nullptr)
		{
			if (active->has_method("_on_execute")) { active->call("_on_execute", parameters); }
			return;
		}
	}

	Node* node = scene->instantiate();
	if (node == nullptr) { return; }

	// An executed cue with a location - a hit spark - goes into the world at that
	// point, so it does not ride along with the character. Everything else hangs
	// off the character.
	Node* parent = target;
	const bool placed = event == CUE_EXECUTED && has_location && target->get_parent() != nullptr;
	if (placed)
	{
		parent = target->get_parent();
	}
	parent->add_child(node);

	if (placed)
	{
		if (Node3D* node_3d = Object::cast_to<Node3D>(node))
		{
			node_3d->set_global_position(parameters["location"]);
		}
		else if (Node2D* node_2d = Object::cast_to<Node2D>(node))
		{
			node_2d->set_global_position(parameters["location"]);
		}
	}

	if (event == CUE_ADDED)
	{
		active_cue_nodes[cue_tag_name] = node->get_instance_id();
		if (node->has_method("_on_active")) { node->call("_on_active", parameters); }
	}
	else if (node->has_method("_on_execute"))
	{
		// It frees itself when it is done - a one-shot particle system on its
		// finished signal, say.
		node->call("_on_execute", parameters);
	}
	else
	{
		// An executed scene owns its own end - Unreal's cue actor calls
		// EndGameplayCue - and one with no _on_execute has no way to have one:
		// nothing else knows it is there, so it would stay for good, one more per
		// execute. Freed at once, and said once, rather than kept on a guessed
		// lifetime.
		if (!warned_cue_scenes.has(cue_tag_name))
		{
			warned_cue_scenes.insert(cue_tag_name);
			WARN_PRINT(vformat("GFGD: gameplay cue \"%s\" was executed with a scene whose root has no _on_execute(parameters); an executed scene has to free itself when done, so it was freed at once. Add _on_execute, or map the tag to a GameplayCueNotify.", String(cue_tag_name)));
		}
		node->queue_free();
	}
}

void AbilitySystemComponent::execute_gameplay_cue(const Ref<GameplayTag>& cue_tag, const Dictionary& parameters)
{
	if (cue_tag.is_valid()) { execute_gameplay_cue_by_name(cue_tag->get_tag_name(), parameters); }
}

void AbilitySystemComponent::add_gameplay_cue(const Ref<GameplayTag>& cue_tag, const Dictionary& parameters)
{
	if (cue_tag.is_valid()) { add_gameplay_cue_by_name(cue_tag->get_tag_name(), parameters); }
}

void AbilitySystemComponent::remove_gameplay_cue(const Ref<GameplayTag>& cue_tag)
{
	if (cue_tag.is_valid()) { remove_gameplay_cue_by_name(cue_tag->get_tag_name()); }
}

void AbilitySystemComponent::execute_gameplay_cue_by_name(const StringName& cue_tag, const Dictionary& parameters, bool skip_owner)
{
	if (cue_tag == StringName()) { return; }

	emit_cue(cue_tag, CUE_EXECUTED, parameters);
	queue_call(skip_owner ? TARGET_ALL_BUT_OWNER : TARGET_ALL, "client_gameplay_cue", Array::make(cue_tag, (int)CUE_EXECUTED, parameters));
}

void AbilitySystemComponent::add_gameplay_cue_by_name(const StringName& cue_tag, const Dictionary& parameters, bool skip_owner)
{
	if (cue_tag == StringName()) { return; }

	int* count = active_cue_counts.getptr(cue_tag);
	if (count != nullptr)
	{
		// Already running from another source; it goes when the last one does.
		(*count)++;
		return;
	}

	active_cue_counts[cue_tag] = 1;
	active_cue_parameters[cue_tag] = parameters;
	emit_cue(cue_tag, CUE_ADDED, parameters);
	queue_call(skip_owner ? TARGET_ALL_BUT_OWNER : TARGET_ALL, "client_gameplay_cue", Array::make(cue_tag, (int)CUE_ADDED, parameters));
}

void AbilitySystemComponent::remove_gameplay_cue_by_name(const StringName& cue_tag, bool skip_owner)
{
	int* count = active_cue_counts.getptr(cue_tag);
	if (count == nullptr) { return; }

	(*count)--;
	if (*count > 0) { return; }

	active_cue_counts.erase(cue_tag);
	active_cue_parameters.erase(cue_tag);
	emit_cue(cue_tag, CUE_REMOVED, Dictionary());
	queue_call(skip_owner ? TARGET_ALL_BUT_OWNER : TARGET_ALL, "client_gameplay_cue", Array::make(cue_tag, (int)CUE_REMOVED, Dictionary()));
}

bool AbilitySystemComponent::is_gameplay_cue_active(const StringName& cue_tag_name) const
{
	return active_cue_counts.has(cue_tag_name);
}

// --- Replication: sending ---

void AbilitySystemComponent::queue_call(int target, const StringName& method, const Array& arguments)
{
	if (!is_replicating_server()) { return; }

	// Held until the end of the frame rather than sent now. A component that
	// spawns applies its startup effects inside add_child, before the NetDriver
	// has told any client the node exists; sent now, those calls would arrive
	// for a node the client has never heard of.
	PendingCall call;
	call.target = target;
	call.method = method;
	call.arguments = arguments;
	pending_calls.push_back(call);
	queue_flush();
}

void AbilitySystemComponent::queue_flush()
{
	if (flush_queued) { return; }
	flush_queued = true;
	callable_mp(this, &AbilitySystemComponent::flush_replication).call_deferred();
}

PackedInt32Array AbilitySystemComponent::resolve_targets(int target) const
{
	PackedInt32Array peers;

	World* world = get_world_node();
	NetDriver* driver = world != nullptr ? world->get_net_driver() : nullptr;
	if (driver == nullptr) { return peers; }

	// Ready peers only: one still loading has not got this node, and it asks for
	// the whole state itself once it has.
	const PackedInt32Array ready = driver->get_ready_peers();
	const int local = world->get_local_peer_id();
	const int owner = get_owner_peer_id();

	for (int i = 0; i < ready.size(); i++)
	{
		const int peer = ready[i];
		if (peer == local) { continue; }

		switch (target)
		{
			case TARGET_ALL:
				peers.push_back(peer);
				break;
			case TARGET_OWNER:
				if (peer == owner) { peers.push_back(peer); }
				break;
			case TARGET_ALL_BUT_OWNER:
				if (peer != owner) { peers.push_back(peer); }
				break;
			case TARGET_EFFECT_VIEWERS:
				if (replication_mode == REPLICATION_FULL || (replication_mode == REPLICATION_MIXED && peer == owner))
				{
					peers.push_back(peer);
				}
				break;
			default:
				if (peer == target) { peers.push_back(peer); }
				break;
		}
	}

	return peers;
}

void AbilitySystemComponent::flush_replication()
{
	flush_queued = false;
	if (!is_inside_tree() || !is_replicating_server())
	{
		pending_calls.clear();
		dirty_attributes.clear();
		dirty_tags.clear();
		return;
	}

	// Tags before attributes before everything else queued this frame: an
	// ability's gating reads tags, a HUD reacting to an effect reads attributes,
	// and both should already be right when the event that changed them lands.
	if (!dirty_tags.is_empty())
	{
		PackedStringArray names;
		PackedInt32Array counts;
		const Array keys = dirty_tags.keys();
		for (int i = 0; i < keys.size(); i++)
		{
			const StringName tag_name = keys[i];
			names.push_back(tag_name);
			counts.push_back(gameplay_tag_count_container->get_tag_count(tag_name));
		}
		dirty_tags.clear();

		const PackedInt32Array peers = resolve_targets(TARGET_ALL);
		for (int i = 0; i < peers.size(); i++)
		{
			rpc_id(peers[i], "client_update_tags", names, counts);
		}
	}

	Ref<AttributeSet> set = get_attribute_set();
	if (!dirty_attributes.is_empty() && set.is_valid())
	{
		PackedStringArray names;
		PackedFloat64Array base_values;
		PackedFloat64Array current_values;
		const Array keys = dirty_attributes.keys();
		for (int i = 0; i < keys.size(); i++)
		{
			const StringName attribute_name = keys[i];
			names.push_back(attribute_name);
			base_values.push_back(set->get_base_value(attribute_name));
			current_values.push_back(set->get_current_value(attribute_name));
		}
		dirty_attributes.clear();

		const PackedInt32Array peers = resolve_targets(TARGET_ALL);
		for (int i = 0; i < peers.size(); i++)
		{
			rpc_id(peers[i], "client_update_attributes", names, base_values, current_values);
		}
	}

	const Vector<PendingCall> calls = pending_calls;
	pending_calls.clear();

	for (int c = 0; c < calls.size(); c++)
	{
		const PendingCall& call = calls[c];
		const PackedInt32Array peers = resolve_targets(call.target);
		for (int i = 0; i < peers.size(); i++)
		{
			Array arguments;
			arguments.push_back(peers[i]);
			arguments.push_back(call.method);
			arguments.append_array(call.arguments);
			callv("rpc_id", arguments);
		}
	}
}

Dictionary AbilitySystemComponent::serialize_effect(const Ref<ActiveGameplayEffect>& active) const
{
	Dictionary data;
	const Ref<GameplayEffect> effect = active->effect;
	data["id"] = active->active_id;
	data["path"] = loadable_path(effect);
	data["policy"] = effect.is_valid() ? (int)effect->get_duration_policy() : 0;
	data["period"] = effect.is_valid() ? effect->get_period() : 0.0f;
	data["duration"] = active->duration;
	data["remaining"] = active->remaining_time;
	data["stack"] = active->stack_count;
	data["level"] = active->get_level();
	data["effect_tags"] = effect.is_valid() ? tag_names_of(effect->get_effect_tags()) : PackedStringArray();
	data["granted_tags"] = effect.is_valid() ? tag_names_of(effect->get_granted_tags()) : PackedStringArray();
	data["cue_tags"] = effect.is_valid() ? tag_names_of(effect->get_gameplay_cue_tags()) : PackedStringArray();
	return data;
}

Dictionary AbilitySystemComponent::serialize_ability(const Ref<GameplayAbility>& ability) const
{
	Dictionary data;
	data["handle"] = ability->get_handle();
	const String* path = ability_template_paths.getptr(ability->get_handle());
	data["path"] = path != nullptr ? *path : String();
	data["name"] = ability->get_ability_name();
	data["input"] = ability->get_input_action_name();
	data["level"] = ability->get_ability_level();
	data["policy"] = (int)ability->get_net_execution_policy();
	data["active"] = ability->get_is_active();
	return data;
}

Dictionary AbilitySystemComponent::build_full_state(int for_peer) const
{
	Dictionary state;

	Dictionary attributes;
	Ref<AttributeSet> set = get_attribute_set();
	if (set.is_valid())
	{
		const PackedStringArray names = set->get_attribute_names();
		for (int i = 0; i < names.size(); i++)
		{
			attributes[names[i]] = Array::make(set->get_base_value(names[i]), set->get_current_value(names[i]));
		}
	}
	state["attributes"] = attributes;

	Dictionary tags;
	const PackedStringArray tag_names = tag_names_of(get_owned_gameplay_tags());
	for (int i = 0; i < tag_names.size(); i++)
	{
		tags[tag_names[i]] = gameplay_tag_count_container->get_tag_count(tag_names[i]);
	}
	state["tags"] = tags;
	state["cues"] = active_cue_parameters.duplicate(true);

	const int owner = get_owner_peer_id();
	const bool sees_effects = replication_mode == REPLICATION_FULL || (replication_mode == REPLICATION_MIXED && for_peer == owner);
	Array effects;
	if (sees_effects)
	{
		for (int i = 0; i < active_effects.size(); i++)
		{
			effects.push_back(serialize_effect(active_effects[i]));
		}
	}
	state["effects"] = effects;

	Array abilities;
	if (for_peer == owner)
	{
		for (int i = 0; i < activatable_abilities.size(); i++)
		{
			const Ref<GameplayAbility> ability = activatable_abilities[i];
			if (ability.is_null()) { continue; }
			abilities.push_back(serialize_ability(ability));
		}
	}
	state["abilities"] = abilities;

	return state;
}

// --- Replication: server receiving ---

void AbilitySystemComponent::server_request_full_state()
{
	if (!is_replicating_server()) { return; }

	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	const int sender = multiplayer.is_valid() ? multiplayer->get_remote_sender_id() : 0;
	if (sender <= 0) { return; }

	// Anyone may ask - the request itself proves the peer has this node, which
	// is all the readiness that matters. Queued behind whatever is already
	// waiting to go out, so the snapshot is never older than a delta after it.
	queue_call(sender, "client_receive_full_state", Array::make(build_full_state(sender)));
}

void AbilitySystemComponent::server_try_activate_ability(int64_t handle, int64_t prediction_key, const StringName& event_tag, const Dictionary& event_data)
{
	if (!is_replicating_server() || !is_sender_owner()) { return; }

	const Ref<GameplayAbility> ability = find_ability_by_handle(handle);

	// An activation claiming to come from an event this ability is not triggered
	// by is not believed. The payload itself is the script's to check.
	const bool event_ok = event_tag == StringName() || (ability.is_valid() && ability->is_triggered_by(event_tag));
	if (ability.is_valid())
	{
		ability->set_trigger_event(event_ok ? event_tag : StringName(), event_ok ? event_data : Dictionary());
	}

	if (ability.is_null() || !event_ok || !ability->can_activate_ability())
	{
		if (ability.is_valid())
		{
			emit_signal("ability_activation_failed", ability, ability->get_last_failure_reason());
		}
		queue_call(TARGET_OWNER, "client_activation_rejected", Array::make(handle, prediction_key));
		return;
	}

	// Remembered on the server's copy too, so data the client sends for this
	// activation can be told from data for an older one.
	ability->set_prediction_key(prediction_key);

	activating_for_client = true;
	ability->activate_ability();
	activating_for_client = false;

	if (prediction_key != 0)
	{
		queue_call(TARGET_OWNER, "client_activation_confirmed", Array::make(handle, prediction_key));
	}
}

void AbilitySystemComponent::server_end_ability(int64_t handle, bool was_canceled)
{
	if (!is_replicating_server() || !is_sender_owner()) { return; }

	const Ref<GameplayAbility> ability = find_ability_by_handle(handle);
	if (ability.is_null() || !ability->get_is_active()) { return; }

	// The client already ended its own copy; it does not need telling.
	applying_remote = true;
	ability->end_from_remote(was_canceled);
	applying_remote = false;
}

void AbilitySystemComponent::server_ability_input(int64_t handle, bool pressed)
{
	if (!is_replicating_server() || !is_sender_owner()) { return; }

	const Ref<GameplayAbility> ability = find_ability_by_handle(handle);
	if (ability.is_null()) { return; }

	ability->set_is_input_pressed(pressed);
	if (!ability->get_is_active()) { return; }

	if (pressed)
	{
		ability->input_pressed();
	}
	else
	{
		ability->input_released();
	}
}

void AbilitySystemComponent::server_ability_target_data(int64_t handle, int64_t prediction_key, int sequence, const Dictionary& data)
{
	if (!is_replicating_server() || !is_sender_owner()) { return; }

	const Ref<GameplayAbility> ability = find_ability_by_handle(handle);
	if (ability.is_valid())
	{
		ability->receive_target_data(prediction_key, sequence, data);
	}
}

void AbilitySystemComponent::send_ability_target_data(const Ref<GameplayAbility>& ability, int sequence, const Dictionary& data)
{
	if (ability.is_null() || !is_replicated_client()) { return; }
	if (ability->get_net_execution_policy() == GameplayAbility::LOCAL_ONLY) { return; }

	rpc_id(World::SERVER_PEER_ID, "server_ability_target_data", ability->get_handle(), ability->get_prediction_key(), sequence, data);
}

bool AbilitySystemComponent::expects_client_target_data(const Ref<GameplayAbility>& ability) const
{
	return ability.is_valid() && is_replicating_server() && owner_is_remote()
			&& ability->get_net_execution_policy() == GameplayAbility::LOCAL_PREDICTED
			&& ability->get_prediction_key() != 0;
}

// --- Gameplay events ---

void AbilitySystemComponent::send_gameplay_event(const StringName& event_tag, const Dictionary& payload)
{
	// Running abilities first, as Unreal does: an ability waiting for this event
	// hears it before any ability it triggers starts.
	emit_signal("gameplay_event", event_tag, payload);

	const Array abilities = activatable_abilities;
	for (int i = 0; i < abilities.size(); i++)
	{
		const Ref<GameplayAbility> ability = abilities[i];
		if (ability.is_null() || ability->get_is_active() || !ability->is_triggered_by(event_tag)) { continue; }
		if (!may_trigger_here(ability)) { continue; }
		if (!ability->should_respond_to_event(event_tag, payload)) { continue; }

		try_activate_ability_internal(ability, event_tag, payload);
	}
}

bool AbilitySystemComponent::may_trigger_here(const Ref<GameplayAbility>& ability) const
{
	World* world = get_world_node();
	const bool networked = replication_mode != REPLICATION_NONE && world != nullptr && world->is_networked();
	if (!networked) { return true; }

	const bool locally_controlled = get_owner_peer_id() == world->get_local_peer_id();

	switch (ability->get_net_execution_policy())
	{
		case GameplayAbility::SERVER_ONLY:
			return world->has_authority();

		case GameplayAbility::LOCAL_ONLY:
		case GameplayAbility::LOCAL_PREDICTED:
		default:
			// The owner's machine triggers it, and a predicted activation carries
			// the event to the server. The server triggering it as well would
			// run it twice.
			return locally_controlled;
	}
}

// --- Replication: client receiving ---

void AbilitySystemComponent::apply_replicated_tag_count(const StringName& tag_name, int count)
{
	const int previous = replicated_tag_counts.has(tag_name) ? replicated_tag_counts[tag_name] : 0;
	if (count <= 0)
	{
		replicated_tag_counts.erase(tag_name);
	}
	else
	{
		replicated_tag_counts[tag_name] = count;
	}

	// A delta rather than a set, so whatever this client added on its own - a
	// predicted ability's tags - survives on top of the server's count.
	if (count != previous)
	{
		update_tag_map(make_tag(tag_name), count - previous);
	}
}

void AbilitySystemComponent::client_update_tags(const PackedStringArray& names, const PackedInt32Array& counts)
{
	if (!is_replicated_client()) { return; }

	const int size = MIN(names.size(), counts.size());
	for (int i = 0; i < size; i++)
	{
		apply_replicated_tag_count(names[i], counts[i]);
	}
}

void AbilitySystemComponent::client_update_attributes(const PackedStringArray& names, const PackedFloat64Array& base_values, const PackedFloat64Array& current_values)
{
	if (!is_replicated_client()) { return; }

	Ref<AttributeSet> set = get_attribute_set();
	if (set.is_null()) { return; }

	const int size = MIN(names.size(), MIN(base_values.size(), current_values.size()));
	for (int i = 0; i < size; i++)
	{
		set->apply_replicated_value(names[i], base_values[i], current_values[i]);
	}
}

Ref<ActiveGameplayEffect> AbilitySystemComponent::build_mirror_effect(const Dictionary& data) const
{
	Ref<GameplayEffect> effect;

	const String path = data.get("path", String());
	if (!path.is_empty() && ResourceLoader::get_singleton()->exists(path))
	{
		effect = ResourceLoader::get_singleton()->load(path);
	}

	if (effect.is_null())
	{
		// Built in code on the server, so there is nothing to load. What the
		// client is given is what it needs to show it: timing and tags.
		effect.instantiate();
		effect->set_duration_policy((GameplayEffect::DurationPolicy)(int)data.get("policy", 0));
		effect->set_duration((float)(double)data.get("duration", 0.0));
		effect->set_period((float)data.get("period", 0.0f));
		effect->set_effect_tags(container_of(data.get("effect_tags", PackedStringArray())));
		effect->set_granted_tags(container_of(data.get("granted_tags", PackedStringArray())));
		effect->set_gameplay_cue_tags(container_of(data.get("cue_tags", PackedStringArray())));
	}

	Ref<ActiveGameplayEffect> active;
	active.instantiate();
	active->effect = effect;
	active->active_id = data.get("id", 0);
	active->duration = data.get("duration", 0.0);
	active->remaining_time = data.get("remaining", 0.0);
	active->stack_count = data.get("stack", 1);
	active->replicated = true;
	return active;
}

void AbilitySystemComponent::client_effect_added(const Dictionary& data)
{
	if (!is_replicated_client()) { return; }

	const int64_t active_id = data.get("id", 0);
	if (find_active_effect_index(active_id) >= 0) { return; }

	const Ref<ActiveGameplayEffect> active = build_mirror_effect(data);
	active_effects.push_back(active);
	emit_signal("gameplay_effect_applied", active->effect, active->active_id);
}

void AbilitySystemComponent::client_effect_updated(int64_t active_id, double duration, double remaining_time, int stack_count)
{
	if (!is_replicated_client()) { return; }

	const int index = find_active_effect_index(active_id);
	if (index < 0) { return; }

	Ref<ActiveGameplayEffect> active = active_effects[index];
	const bool stack_changed = active->stack_count != stack_count;
	active->duration = duration;
	active->remaining_time = remaining_time;
	active->stack_count = stack_count;

	if (stack_changed)
	{
		emit_signal("gameplay_effect_stack_changed", active->effect, active_id, stack_count);
	}
}

void AbilitySystemComponent::client_effect_removed(int64_t active_id)
{
	if (!is_replicated_client()) { return; }

	const int index = find_active_effect_index(active_id);
	if (index < 0) { return; }

	Ref<ActiveGameplayEffect> active = active_effects[index];
	active_effects.remove_at(index);
	emit_signal("gameplay_effect_removed", active->effect, active_id);
}

void AbilitySystemComponent::client_ability_given(const Dictionary& data)
{
	if (!is_replicated_client()) { return; }

	const int64_t handle = data.get("handle", 0);
	if (handle == 0 || find_ability_by_handle(handle).is_valid()) { return; }

	Ref<GameplayAbility> ability;

	const String path = data.get("path", String());
	if (!path.is_empty() && ResourceLoader::get_singleton()->exists(path))
	{
		const Ref<GameplayAbility> ability_template = ResourceLoader::get_singleton()->load(path);
		if (ability_template.is_valid())
		{
			ability = ability_template->duplicate();
		}
	}

	if (ability.is_null())
	{
		// Granted from a template built in code: there is nothing to load, so the
		// client gets a stand-in that can be listed and asked for, but has no
		// script of its own to predict with.
		ability.instantiate();
		ability->set_ability_name(data.get("name", StringName()));
		ability->set_input_action_name(data.get("input", StringName()));
		ability->set_net_execution_policy((GameplayAbility::NetExecutionPolicy)(int)data.get("policy", (int)GameplayAbility::LOCAL_PREDICTED));
	}

	ability->set_handle(handle);
	ability->set_ability_level(data.get("level", 1.0f));
	activatable_abilities.append(ability);
	ability->setup_ability(this, Variant());
	ability->on_give_ability();

	emit_signal("ability_given", ability);

	// Joining while a server-only ability runs: show it running.
	if ((bool)data.get("active", false) && ability->get_net_execution_policy() == GameplayAbility::SERVER_ONLY)
	{
		applying_remote = true;
		ability->activate_from_remote();
		applying_remote = false;
	}
}

void AbilitySystemComponent::client_ability_removed(int64_t handle)
{
	if (!is_replicated_client()) { return; }

	const Ref<GameplayAbility> ability = find_ability_by_handle(handle);
	if (ability.is_null()) { return; }

	if (ability->get_is_active())
	{
		applying_remote = true;
		ability->end_from_remote(true);
		applying_remote = false;
	}

	activatable_abilities.erase(ability);
	ability->on_remove_ability();
}

void AbilitySystemComponent::client_ability_activated(int64_t handle)
{
	if (!is_replicated_client()) { return; }

	const Ref<GameplayAbility> ability = find_ability_by_handle(handle);
	if (ability.is_null() || ability->get_is_active()) { return; }

	applying_remote = true;
	ability->activate_from_remote();
	applying_remote = false;
}

void AbilitySystemComponent::client_ability_ended(int64_t handle, bool was_canceled)
{
	if (!is_replicated_client()) { return; }

	const Ref<GameplayAbility> ability = find_ability_by_handle(handle);
	if (ability.is_null() || !ability->get_is_active()) { return; }

	applying_remote = true;
	ability->end_from_remote(was_canceled);
	applying_remote = false;
}

void AbilitySystemComponent::client_activation_confirmed(int64_t handle, int64_t prediction_key)
{
	if (!is_replicated_client()) { return; }

	const Ref<GameplayAbility> ability = find_ability_by_handle(handle);
	if (ability.is_valid() && ability->get_prediction_key() == prediction_key)
	{
		ability->set_predicting(false);
	}
}

void AbilitySystemComponent::client_activation_rejected(int64_t handle, int64_t prediction_key)
{
	if (!is_replicated_client()) { return; }

	const Ref<GameplayAbility> ability = find_ability_by_handle(handle);
	if (ability.is_null()) { return; }

	// Only the activation this answer is about. A newer one, started after the
	// refused one ended, is not undone by it.
	if (ability->get_is_active() && (prediction_key == 0 || ability->get_prediction_key() == prediction_key))
	{
		applying_remote = true;
		ability->end_from_remote(true);
		applying_remote = false;
	}

	ability->set_predicting(false);
	ability->set_last_failure_reason("rejected");
	emit_signal("ability_activation_failed", ability, StringName("rejected"));
}

void AbilitySystemComponent::client_gameplay_cue(const StringName& cue_tag_name, int event_type, const Dictionary& parameters)
{
	if (!is_replicated_client()) { return; }

	switch (event_type)
	{
		case CUE_ADDED:
			active_cue_counts[cue_tag_name] = 1;
			active_cue_parameters[cue_tag_name] = parameters;
			break;
		case CUE_REMOVED:
			active_cue_counts.erase(cue_tag_name);
			active_cue_parameters.erase(cue_tag_name);
			break;
		default:
			break;
	}

	emit_cue(cue_tag_name, (CueEvent)event_type, parameters);
}

void AbilitySystemComponent::client_receive_full_state(const Dictionary& state)
{
	if (!is_replicated_client()) { return; }

	// Attributes.
	Ref<AttributeSet> set = get_attribute_set();
	const Dictionary attributes = state.get("attributes", Dictionary());
	if (set.is_valid())
	{
		const Array names = attributes.keys();
		for (int i = 0; i < names.size(); i++)
		{
			const Array values = attributes[names[i]];
			if (values.size() >= 2)
			{
				set->apply_replicated_value(names[i], values[0], values[1]);
			}
		}
	}

	// Tags: whatever the server no longer has goes, then everything it has.
	const Dictionary tags = state.get("tags", Dictionary());
	Vector<StringName> stale;
	for (const KeyValue<StringName, int>& entry : replicated_tag_counts)
	{
		if (!tags.has(entry.key)) { stale.push_back(entry.key); }
	}
	for (int i = 0; i < stale.size(); i++)
	{
		apply_replicated_tag_count(stale[i], 0);
	}
	const Array tag_names = tags.keys();
	for (int i = 0; i < tag_names.size(); i++)
	{
		apply_replicated_tag_count(tag_names[i], tags[tag_names[i]]);
	}

	// Cues.
	const Dictionary cues = state.get("cues", Dictionary());
	const Array running = active_cue_parameters.keys();
	for (int i = 0; i < running.size(); i++)
	{
		if (!cues.has(running[i]))
		{
			client_gameplay_cue(running[i], CUE_REMOVED, Dictionary());
		}
	}
	const Array cue_names = cues.keys();
	for (int i = 0; i < cue_names.size(); i++)
	{
		if (!active_cue_counts.has(cue_names[i]))
		{
			client_gameplay_cue(cue_names[i], CUE_ADDED, cues[cue_names[i]]);
		}
	}

	// Effects: the mirror is rebuilt.
	for (int i = active_effects.size() - 1; i >= 0; i--)
	{
		client_effect_removed(active_effects[i]->active_id);
	}
	const Array effects = state.get("effects", Array());
	for (int i = 0; i < effects.size(); i++)
	{
		client_effect_added(effects[i]);
	}

	// Abilities: the ones the server no longer grants go, the missing ones come.
	const Array abilities = state.get("abilities", Array());
	Vector<int64_t> handles;
	for (int i = 0; i < abilities.size(); i++)
	{
		const Dictionary data = abilities[i];
		handles.push_back(data.get("handle", 0));
	}
	const Array current = activatable_abilities.duplicate();
	for (int i = 0; i < current.size(); i++)
	{
		const Ref<GameplayAbility> ability = current[i];
		if (ability.is_valid() && !handles.has(ability->get_handle()))
		{
			client_ability_removed(ability->get_handle());
		}
	}
	for (int i = 0; i < abilities.size(); i++)
	{
		client_ability_given(abilities[i]);
	}

	replication_state_received = true;
	emit_signal("replicated_state_received");
}

void AbilitySystemComponent::_bind_methods()
{
	BIND_ENUM_CONSTANT(REPLICATION_NONE);
	BIND_ENUM_CONSTANT(REPLICATION_MINIMAL);
	BIND_ENUM_CONSTANT(REPLICATION_MIXED);
	BIND_ENUM_CONSTANT(REPLICATION_FULL);

	BIND_ENUM_CONSTANT(CUE_EXECUTED);
	BIND_ENUM_CONSTANT(CUE_ADDED);
	BIND_ENUM_CONSTANT(CUE_REMOVED);

	ClassDB::bind_method(D_METHOD("give_ability", "ability_template", "source_object", "level"), &AbilitySystemComponent::give_ability, DEFVAL(Variant()), DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("clear_ability", "ability"), &AbilitySystemComponent::clear_ability);
	ClassDB::bind_method(D_METHOD("clear_all_abilities"), &AbilitySystemComponent::clear_all_abilities);
	ClassDB::bind_method(D_METHOD("try_activate_ability", "ability_name"), &AbilitySystemComponent::try_activate_ability);
	ClassDB::bind_method(D_METHOD("try_activate_ability_by_handle", "handle"), &AbilitySystemComponent::try_activate_ability_by_handle);
	ClassDB::bind_method(D_METHOD("cancel_abilities_with_tags", "tag_container"), &AbilitySystemComponent::cancel_abilities_with_tags);
	ClassDB::bind_method(D_METHOD("cancel_all_abilities"), &AbilitySystemComponent::cancel_all_abilities);
	ClassDB::bind_method(D_METHOD("get_activatable_abilities"), &AbilitySystemComponent::get_activatable_abilities);
	ClassDB::bind_method(D_METHOD("find_ability_by_name", "ability_name"), &AbilitySystemComponent::find_ability_by_name);
	ClassDB::bind_method(D_METHOD("find_ability_by_handle", "handle"), &AbilitySystemComponent::find_ability_by_handle);

	ClassDB::bind_method(D_METHOD("get_owned_gameplay_tags"), &AbilitySystemComponent::get_owned_gameplay_tags);
	ClassDB::bind_method(D_METHOD("get_blocked_ability_tags"), &AbilitySystemComponent::get_blocked_ability_tags);

	ClassDB::bind_method(D_METHOD("update_tag_map", "tag", "count_delta"), &AbilitySystemComponent::update_tag_map);
	ClassDB::bind_method(D_METHOD("register_gameplay_tag_event", "tag", "tag_delegate"), &AbilitySystemComponent::register_gameplay_tag_event);
	ClassDB::bind_method(D_METHOD("update_blocked_ability_tags", "tag", "count_delta"), &AbilitySystemComponent::update_blocked_ability_tags);
	ClassDB::bind_method(D_METHOD("has_matching_gameplay_tag", "tag"), &AbilitySystemComponent::has_matching_gameplay_tag);
	ClassDB::bind_method(D_METHOD("has_any_matching_gameplay_tags", "tag_container"), &AbilitySystemComponent::has_any_matching_gameplay_tags);
	ClassDB::bind_method(D_METHOD("has_all_matching_gameplay_tags", "tag_container"), &AbilitySystemComponent::has_all_matching_gameplay_tags);
	ClassDB::bind_method(D_METHOD("get_gameplay_tag_count", "tag"), &AbilitySystemComponent::get_gameplay_tag_count);
	ClassDB::bind_method(D_METHOD("add_loose_gameplay_tag", "tag", "count"), &AbilitySystemComponent::add_loose_gameplay_tag, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("remove_loose_gameplay_tag", "tag", "count"), &AbilitySystemComponent::remove_loose_gameplay_tag, DEFVAL(1));

	ClassDB::bind_method(D_METHOD("ability_local_input_pressed", "action_name"), &AbilitySystemComponent::ability_local_input_pressed);
	ClassDB::bind_method(D_METHOD("ability_local_input_released", "action_name"), &AbilitySystemComponent::ability_local_input_released);

	ClassDB::bind_method(D_METHOD("make_outgoing_spec", "effect", "level"), &AbilitySystemComponent::make_outgoing_spec, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("apply_gameplay_effect_spec_to_self", "spec"), &AbilitySystemComponent::apply_gameplay_effect_spec_to_self);
	ClassDB::bind_method(D_METHOD("apply_gameplay_effect_to_self", "effect", "level"), &AbilitySystemComponent::apply_gameplay_effect_to_self, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("apply_gameplay_effect_spec_to_target", "spec", "target"), &AbilitySystemComponent::apply_gameplay_effect_spec_to_target);
	ClassDB::bind_method(D_METHOD("apply_gameplay_effect_to_target", "effect", "target", "level"), &AbilitySystemComponent::apply_gameplay_effect_to_target, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("remove_active_gameplay_effect", "active_id", "stacks_to_remove"), &AbilitySystemComponent::remove_active_gameplay_effect, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("remove_active_effects_with_tags", "tag_container"), &AbilitySystemComponent::remove_active_effects_with_tags);
	ClassDB::bind_method(D_METHOD("get_active_effects"), &AbilitySystemComponent::get_active_effects);
	ClassDB::bind_method(D_METHOD("get_active_effect", "active_id"), &AbilitySystemComponent::get_active_effect);
	ClassDB::bind_method(D_METHOD("get_active_effect_stack_count", "active_id"), &AbilitySystemComponent::get_active_effect_stack_count);
	ClassDB::bind_method(D_METHOD("get_active_effect_remaining_time", "active_id"), &AbilitySystemComponent::get_active_effect_remaining_time);
	ClassDB::bind_method(D_METHOD("get_effects_time_remaining_with_granted_tags", "tag_container"), &AbilitySystemComponent::get_effects_time_remaining_with_granted_tags);

	ClassDB::bind_method(D_METHOD("get_attribute_set"), &AbilitySystemComponent::get_attribute_set);
	ClassDB::bind_method(D_METHOD("get_attribute_value", "attribute_name"), &AbilitySystemComponent::get_attribute_value);
	ClassDB::bind_method(D_METHOD("get_attribute_base_value", "attribute_name"), &AbilitySystemComponent::get_attribute_base_value);
	ClassDB::bind_method(D_METHOD("set_attribute_base_value", "attribute_name", "value"), &AbilitySystemComponent::set_attribute_base_value);

	ClassDB::bind_method(D_METHOD("execute_gameplay_cue", "cue_tag", "parameters"), &AbilitySystemComponent::execute_gameplay_cue, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("add_gameplay_cue", "cue_tag", "parameters"), &AbilitySystemComponent::add_gameplay_cue, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("remove_gameplay_cue", "cue_tag"), &AbilitySystemComponent::remove_gameplay_cue);
	ClassDB::bind_method(D_METHOD("is_gameplay_cue_active", "cue_tag_name"), &AbilitySystemComponent::is_gameplay_cue_active);
	ClassDB::bind_method(D_METHOD("execute_gameplay_cue_by_name", "cue_tag", "parameters", "skip_owner"), &AbilitySystemComponent::execute_gameplay_cue_by_name, DEFVAL(Dictionary()), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("add_gameplay_cue_by_name", "cue_tag", "parameters", "skip_owner"), &AbilitySystemComponent::add_gameplay_cue_by_name, DEFVAL(Dictionary()), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("remove_gameplay_cue_by_name", "cue_tag", "skip_owner"), &AbilitySystemComponent::remove_gameplay_cue_by_name, DEFVAL(false));

	ClassDB::bind_method(D_METHOD("has_authority"), &AbilitySystemComponent::has_authority);
	ClassDB::bind_method(D_METHOD("is_replicated_client"), &AbilitySystemComponent::is_replicated_client);
	ClassDB::bind_method(D_METHOD("can_apply_effects"), &AbilitySystemComponent::can_apply_effects);
	ClassDB::bind_method(D_METHOD("runs_ability_logic", "ability"), &AbilitySystemComponent::runs_ability_logic);
	ClassDB::bind_method(D_METHOD("get_owner_peer_id"), &AbilitySystemComponent::get_owner_peer_id);
	ClassDB::bind_method(D_METHOD("is_replication_state_received"), &AbilitySystemComponent::is_replication_state_received);

	ClassDB::bind_method(D_METHOD("server_request_full_state"), &AbilitySystemComponent::server_request_full_state);
	ClassDB::bind_method(D_METHOD("server_try_activate_ability", "handle", "prediction_key", "event_tag", "event_data"), &AbilitySystemComponent::server_try_activate_ability);
	ClassDB::bind_method(D_METHOD("server_end_ability", "handle", "was_canceled"), &AbilitySystemComponent::server_end_ability);
	ClassDB::bind_method(D_METHOD("server_ability_input", "handle", "pressed"), &AbilitySystemComponent::server_ability_input);
	ClassDB::bind_method(D_METHOD("server_ability_target_data", "handle", "prediction_key", "sequence", "data"), &AbilitySystemComponent::server_ability_target_data);
	ClassDB::bind_method(D_METHOD("send_gameplay_event", "event_tag", "payload"), &AbilitySystemComponent::send_gameplay_event, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("client_receive_full_state", "state"), &AbilitySystemComponent::client_receive_full_state);
	ClassDB::bind_method(D_METHOD("client_update_attributes", "names", "base_values", "current_values"), &AbilitySystemComponent::client_update_attributes);
	ClassDB::bind_method(D_METHOD("client_update_tags", "names", "counts"), &AbilitySystemComponent::client_update_tags);
	ClassDB::bind_method(D_METHOD("client_effect_added", "data"), &AbilitySystemComponent::client_effect_added);
	ClassDB::bind_method(D_METHOD("client_effect_updated", "active_id", "duration", "remaining_time", "stack_count"), &AbilitySystemComponent::client_effect_updated);
	ClassDB::bind_method(D_METHOD("client_effect_removed", "active_id"), &AbilitySystemComponent::client_effect_removed);
	ClassDB::bind_method(D_METHOD("client_ability_given", "data"), &AbilitySystemComponent::client_ability_given);
	ClassDB::bind_method(D_METHOD("client_ability_removed", "handle"), &AbilitySystemComponent::client_ability_removed);
	ClassDB::bind_method(D_METHOD("client_ability_activated", "handle"), &AbilitySystemComponent::client_ability_activated);
	ClassDB::bind_method(D_METHOD("client_ability_ended", "handle", "was_canceled"), &AbilitySystemComponent::client_ability_ended);
	ClassDB::bind_method(D_METHOD("client_activation_confirmed", "handle", "prediction_key"), &AbilitySystemComponent::client_activation_confirmed);
	ClassDB::bind_method(D_METHOD("client_activation_rejected", "handle", "prediction_key"), &AbilitySystemComponent::client_activation_rejected);
	ClassDB::bind_method(D_METHOD("client_gameplay_cue", "cue_tag_name", "event_type", "parameters"), &AbilitySystemComponent::client_gameplay_cue);

	ClassDB::bind_method(D_METHOD("set_startup_abilities", "value"), &AbilitySystemComponent::set_startup_abilities);
	ClassDB::bind_method(D_METHOD("get_startup_abilities"), &AbilitySystemComponent::get_startup_abilities);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "startup_abilities", PROPERTY_HINT_ARRAY_TYPE, vformat("%d/%d:GameplayAbility", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_startup_abilities", "get_startup_abilities");

	ClassDB::bind_method(D_METHOD("set_startup_effects", "value"), &AbilitySystemComponent::set_startup_effects);
	ClassDB::bind_method(D_METHOD("get_startup_effects"), &AbilitySystemComponent::get_startup_effects);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "startup_effects", PROPERTY_HINT_ARRAY_TYPE, vformat("%d/%d:GameplayEffect", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_startup_effects", "get_startup_effects");

	ClassDB::bind_method(D_METHOD("set_attribute_set_template", "value"), &AbilitySystemComponent::set_attribute_set_template);
	ClassDB::bind_method(D_METHOD("get_attribute_set_template"), &AbilitySystemComponent::get_attribute_set_template);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "attribute_set", PROPERTY_HINT_RESOURCE_TYPE, "AttributeSet"), "set_attribute_set_template", "get_attribute_set_template");

	ClassDB::bind_method(D_METHOD("set_replication_mode", "value"), &AbilitySystemComponent::set_replication_mode);
	ClassDB::bind_method(D_METHOD("get_replication_mode"), &AbilitySystemComponent::get_replication_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "replication_mode", PROPERTY_HINT_ENUM, "None,Minimal,Mixed,Full"), "set_replication_mode", "get_replication_mode");

	ADD_SIGNAL(MethodInfo("ability_given", PropertyInfo(Variant::OBJECT, "ability", PROPERTY_HINT_RESOURCE_TYPE, "GameplayAbility")));
	ADD_SIGNAL(MethodInfo("ability_activated", PropertyInfo(Variant::OBJECT, "ability", PROPERTY_HINT_RESOURCE_TYPE, "GameplayAbility")));
	ADD_SIGNAL(MethodInfo("ability_activation_failed", PropertyInfo(Variant::OBJECT, "ability", PROPERTY_HINT_RESOURCE_TYPE, "GameplayAbility"), PropertyInfo(Variant::STRING_NAME, "reason")));
	ADD_SIGNAL(MethodInfo("ability_ended", PropertyInfo(Variant::OBJECT, "ability", PROPERTY_HINT_RESOURCE_TYPE, "GameplayAbility"), PropertyInfo(Variant::BOOL, "was_canceled")));
	ADD_SIGNAL(MethodInfo("owned_tag_changed", PropertyInfo(Variant::STRING_NAME, "tag_name"), PropertyInfo(Variant::INT, "new_count")));
	ADD_SIGNAL(MethodInfo("gameplay_effect_applied", PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "GameplayEffect"), PropertyInfo(Variant::INT, "active_id")));
	ADD_SIGNAL(MethodInfo("gameplay_effect_removed", PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "GameplayEffect"), PropertyInfo(Variant::INT, "active_id")));
	ADD_SIGNAL(MethodInfo("gameplay_effect_stack_changed", PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "GameplayEffect"), PropertyInfo(Variant::INT, "active_id"), PropertyInfo(Variant::INT, "stack_count")));
	ADD_SIGNAL(MethodInfo("attribute_changed", PropertyInfo(Variant::STRING_NAME, "attribute_name"), PropertyInfo(Variant::FLOAT, "old_value"), PropertyInfo(Variant::FLOAT, "new_value")));
	ADD_SIGNAL(MethodInfo("gameplay_cue", PropertyInfo(Variant::STRING_NAME, "cue_tag"), PropertyInfo(Variant::INT, "event_type"), PropertyInfo(Variant::DICTIONARY, "parameters")));
	ADD_SIGNAL(MethodInfo("replicated_state_received"));
	ADD_SIGNAL(MethodInfo("gameplay_event", PropertyInfo(Variant::STRING_NAME, "event_tag"), PropertyInfo(Variant::DICTIONARY, "payload")));
}
}
