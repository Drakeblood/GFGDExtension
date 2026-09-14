#ifndef ABILITY_SYSTEM_COMPONENT_H
#define ABILITY_SYSTEM_COMPONENT_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include "ability_system/attribute_set.h"
#include "ability_system/active_gameplay_effect.h"
#include "ability_system/gameplay_effect_spec.h"

using namespace godot;

namespace GFGD
{
class GameplayTag;
class GameplayAbility;
class AbilityAsync;
class GameplayTagCountContainer;
class GameplayTagContainer;
class GameplayEffect;
class World;

// The hub of the ability system: attributes, the effects changing them, the
// abilities a character has, and the tags all three read and write.
//
// Networked, the server's component is the truth and every client's is a
// mirror of it (see replication_mode). Attributes, owned tags and gameplay cues
// reach every peer; active effects reach the owner, or everyone; the granted
// abilities reach the owner, who may ask the server to activate them - and, for
// a LOCAL_PREDICTED ability, run it at once without waiting for the answer.
class AbilitySystemComponent : public Node
{
	GDCLASS(AbilitySystemComponent, Node)

public:
	// How much of this component a client is sent. Unreal's
	// EGameplayEffectReplicationMode, with NONE for a component that should
	// stay local to every machine, as all of them did before replication.
	enum ReplicationMode
	{
		// Nothing is sent. Every peer runs its own component, as offline.
		REPLICATION_NONE = 0,

		// Attributes, owned tags and gameplay cues to everyone; the ability list
		// to the owner. Active effects to nobody.
		REPLICATION_MINIMAL = 1,

		// As MINIMAL, and active effects to the owner - a player's own HUD shows
		// its buffs, nobody else's does.
		REPLICATION_MIXED = 2,

		// As MINIMAL, and active effects to everyone.
		REPLICATION_FULL = 3,
	};

	enum CueEvent
	{
		// Something happened once: an instant effect, a period tick, a hit.
		CUE_EXECUTED = 0,

		// Something started and stays until CUE_REMOVED: a burning status.
		CUE_ADDED = 1,
		CUE_REMOVED = 2,
	};

private:
	Array startup_abilities;
	Array startup_effects;
	Ref<AttributeSet> attribute_set;
	ReplicationMode replication_mode;

	Array activatable_abilities;
	GameplayTagCountContainer* gameplay_tag_count_container;
	GameplayTagCountContainer* blocked_ability_tags;

	Ref<AttributeSet> runtime_attribute_set;
	Vector<Ref<ActiveGameplayEffect>> active_effects;
	int64_t next_active_effect_id;
	int64_t next_ability_handle;
	int64_t next_prediction_key;

	// Where each granted ability's template was loaded from, by handle, so the
	// owning client can build the same ability - with its script, cost and
	// cooldown - rather than a nameplate.
	HashMap<int64_t, String> ability_template_paths;

	// Persistent cues this component has running, and what they were started
	// with, so a peer arriving late is handed them too.
	HashMap<StringName, int> active_cue_counts;
	Dictionary active_cue_parameters;

	// The node a scene handler instanced for each cue while it is active.
	HashMap<StringName, ObjectID> active_cue_nodes;

	// Cue tags already warned about for an executed scene with no _on_execute.
	HashSet<StringName> warned_cue_scenes;

	// --- Replication, server side ---

	struct PendingCall
	{
		// A peer id, or one of the TARGET_* below, resolved when sent.
		int target = 0;
		StringName method;
		Array arguments;
	};

	Vector<PendingCall> pending_calls;
	Dictionary dirty_attributes;
	Dictionary dirty_tags;
	bool flush_queued;

	// Set while the server runs an activation a client asked for, so it does not
	// tell that client something it already knows.
	bool activating_for_client;

	// --- Replication, client side ---

	// The owned tag counts as the server last sent them. What the client owns is
	// these plus whatever it added itself - a predicted ability's tags.
	HashMap<StringName, int> replicated_tag_counts;
	bool replication_state_received;

	// Set while applying something the other side already did, so it is not
	// sent back.
	bool applying_remote;

	Array asyncs;

public:
	AbilitySystemComponent();
	~AbilitySystemComponent();

	virtual void _ready() override;
	virtual void _physics_process(double delta) override;

	// --- AbilityAsync ---

	// The asyncs watching this component. Held here, so one nobody kept a
	// variable for still lives; all of them end when the component is freed.
	void register_async(AbilityAsync* async);
	void unregister_async(AbilityAsync* async);

	// --- Abilities ---

	// Returns the ability's handle - the same number on the server and on the
	// owning client - or 0 when nothing was granted.
	int64_t give_ability(const Ref<GameplayAbility>& ability_template, const Variant& source_object = Variant(), float level = 1.0f);
	void clear_ability(const Ref<GameplayAbility>& ability);
	void clear_all_abilities();
	bool try_activate_ability(const StringName& ability_name);
	bool try_activate_ability_by_handle(int64_t handle);
	void cancel_abilities_with_tags(const Ref<GameplayTagContainer>& tag_container);
	void cancel_all_abilities();
	Array get_activatable_abilities() const { return activatable_abilities; }
	Ref<GameplayAbility> find_ability_by_name(const StringName& ability_name) const;
	Ref<GameplayAbility> find_ability_by_handle(int64_t handle) const;

	void ability_local_input_pressed(const StringName& action_name);
	void ability_local_input_released(const StringName& action_name);

	// Internal notifications from GameplayAbility.
	void notify_ability_activated(const Ref<GameplayAbility>& ability);
	void notify_ability_ended(const Ref<GameplayAbility>& ability, bool was_canceled);

	// --- Tags ---
	Ref<GameplayTagContainer> get_owned_gameplay_tags() const;
	Ref<GameplayTagContainer> get_blocked_ability_tags() const;

	void update_tag_map(const Ref<GameplayTag>& tag, int count_delta);
	void register_gameplay_tag_event(const Ref<GameplayTag>& tag, const Callable& tag_delegate);
	void update_blocked_ability_tags(const Ref<GameplayTag>& tag, int count_delta);

	bool has_matching_gameplay_tag(const Ref<GameplayTag>& tag) const;
	bool has_any_matching_gameplay_tags(const Ref<GameplayTagContainer>& tag_container) const;
	bool has_all_matching_gameplay_tags(const Ref<GameplayTagContainer>& tag_container) const;
	int get_gameplay_tag_count(const Ref<GameplayTag>& tag) const;

	// Tags with no effect or ability behind them - "Stunned" set by a script.
	// They count like any other source, so a loose tag and an effect granting the
	// same tag both have to let go of it.
	void add_loose_gameplay_tag(const Ref<GameplayTag>& tag, int count = 1);
	void remove_loose_gameplay_tag(const Ref<GameplayTag>& tag, int count = 1);

	// --- Effects & attributes ---

	Ref<GameplayEffectSpec> make_outgoing_spec(const Ref<GameplayEffect>& effect, float level = 1.0f);

	// Returns -1 when application was blocked, 0 for instant effects,
	// and the active effect id (> 0) for duration/infinite effects.
	int64_t apply_gameplay_effect_spec_to_self(const Ref<GameplayEffectSpec>& spec);
	int64_t apply_gameplay_effect_to_self(const Ref<GameplayEffect>& effect, float level = 1.0f);
	int64_t apply_gameplay_effect_spec_to_target(const Ref<GameplayEffectSpec>& spec, AbilitySystemComponent* target);
	int64_t apply_gameplay_effect_to_target(const Ref<GameplayEffect>& effect, AbilitySystemComponent* target, float level = 1.0f);

	// stacks_to_remove below 1 removes the whole effect.
	bool remove_active_gameplay_effect(int64_t active_id, int stacks_to_remove = -1);
	int remove_active_effects_with_tags(const Ref<GameplayTagContainer>& tag_container);
	Array get_active_effects() const;
	Ref<ActiveGameplayEffect> get_active_effect(int64_t active_id) const;
	int get_active_effect_stack_count(int64_t active_id) const;
	double get_active_effect_remaining_time(int64_t active_id) const;

	// The longest time left among active effects granting any of these tags -
	// how long a cooldown has to go.
	double get_effects_time_remaining_with_granted_tags(const Ref<GameplayTagContainer>& tag_container) const;

	Ref<AttributeSet> get_attribute_set() const;
	double get_attribute_value(const StringName& attribute_name) const;
	double get_attribute_base_value(const StringName& attribute_name) const;
	void set_attribute_base_value(const StringName& attribute_name, double value);

	// --- Gameplay cues ---

	// Cues are for what a player sees and hears, never for game state: an
	// executed cue a peer was not connected for is simply gone (only added ones
	// are handed to a late joiner), and a client may fire one of its own that
	// nobody else hears. Fired on the server, they go to every peer.
	void execute_gameplay_cue(const Ref<GameplayTag>& cue_tag, const Dictionary& parameters = Dictionary());
	void add_gameplay_cue(const Ref<GameplayTag>& cue_tag, const Dictionary& parameters = Dictionary());
	void remove_gameplay_cue(const Ref<GameplayTag>& cue_tag);
	bool is_gameplay_cue_active(const StringName& cue_tag_name) const;

	// The same, by tag name. skip_owner leaves the owning client out of what the
	// server sends - for a cue the owner already played itself, as a predicting
	// client does. GameplayAbility's cue methods set it for you.
	void execute_gameplay_cue_by_name(const StringName& cue_tag, const Dictionary& parameters = Dictionary(), bool skip_owner = false);
	void add_gameplay_cue_by_name(const StringName& cue_tag, const Dictionary& parameters = Dictionary(), bool skip_owner = false);
	void remove_gameplay_cue_by_name(const StringName& cue_tag, bool skip_owner = false);

	// --- Gameplay events ---

	// Tells this component's running abilities that something happened -
	// "Event.Montage.Hit" with the hit in the payload. Local to this machine: a
	// server-side event reaches the server's abilities only.
	void send_gameplay_event(const StringName& event_tag, const Dictionary& payload = Dictionary());

	// --- Networking ---

	// Used by sync_target_data.
	void send_ability_target_data(const Ref<GameplayAbility>& ability, int sequence, const Dictionary& data);
	bool expects_client_target_data(const Ref<GameplayAbility>& ability) const;

	ReplicationMode get_replication_mode() const { return replication_mode; }
	void set_replication_mode(ReplicationMode value) { replication_mode = value; }

	// True on the server, offline, and for a component nothing replicates.
	bool has_authority() const;

	// True on a client whose component mirrors the server's.
	bool is_replicated_client() const;

	// Whether effects may be applied here - everywhere but a replicated client,
	// where the server's results arrive instead.
	bool can_apply_effects() const { return !is_replicated_client(); }

	// Whether an ability's script and tags run on this machine: everywhere but a
	// replicated client for a SERVER_ONLY ability.
	bool runs_ability_logic(const Ref<GameplayAbility>& ability) const;

	int get_owner_peer_id() const;
	bool is_replication_state_received() const { return replication_state_received; }

	// Remote calls. Public because the multiplayer API calls them by name; not
	// for game code.
	void server_request_full_state();
	void server_try_activate_ability(int64_t handle, int64_t prediction_key, const StringName& event_tag, const Dictionary& event_data);
	void server_end_ability(int64_t handle, bool was_canceled);
	void server_ability_input(int64_t handle, bool pressed);
	void server_ability_target_data(int64_t handle, int64_t prediction_key, int sequence, const Dictionary& data);

	void client_receive_full_state(const Dictionary& state);
	void client_update_attributes(const PackedStringArray& names, const PackedFloat64Array& base_values, const PackedFloat64Array& current_values);
	void client_update_tags(const PackedStringArray& names, const PackedInt32Array& counts);
	void client_effect_added(const Dictionary& data);
	void client_effect_updated(int64_t active_id, double duration, double remaining_time, int stack_count);
	void client_effect_removed(int64_t active_id);
	void client_ability_given(const Dictionary& data);
	void client_ability_removed(int64_t handle);
	void client_ability_activated(int64_t handle);
	void client_ability_ended(int64_t handle, bool was_canceled);
	void client_activation_confirmed(int64_t handle, int64_t prediction_key);
	void client_activation_rejected(int64_t handle, int64_t prediction_key);
	void client_gameplay_cue(const StringName& cue_tag_name, int event_type, const Dictionary& parameters);

	// --- Editor properties ---
	void set_startup_abilities(const Array& value) { startup_abilities = value; }
	Array get_startup_abilities() const { return startup_abilities; }
	void set_startup_effects(const Array& value) { startup_effects = value; }
	Array get_startup_effects() const { return startup_effects; }
	void set_attribute_set_template(const Ref<AttributeSet>& value) { attribute_set = value; }
	Ref<AttributeSet> get_attribute_set_template() const { return attribute_set; }

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	static constexpr int TARGET_ALL = 0;
	static constexpr int TARGET_OWNER = -1;
	static constexpr int TARGET_EFFECT_VIEWERS = -2;
	static constexpr int TARGET_ALL_BUT_OWNER = -3;

	World* get_world_node() const;
	bool is_replicating_server() const;
	bool owner_is_remote() const;

	void on_tag_updated(const Ref<GameplayTag>& tag, bool tag_exists);
	void _on_ability_ended_clear(bool was_canceled, const Ref<GameplayAbility>& ability);
	void _on_attribute_set_changed(const StringName& attribute_name, double old_value, double new_value);
	void _on_attribute_base_changed(const StringName& attribute_name, double old_value, double new_value);

	bool try_activate_ability_internal(const Ref<GameplayAbility>& ability, const StringName& event_tag = StringName(), const Dictionary& event_data = Dictionary());

	// Unreal's HasNetworkAuthorityToActivateTriggeredAbility: a local or
	// predicted ability is triggered where its owner is controlled, a
	// server-only one on the server.
	bool may_trigger_here(const Ref<GameplayAbility>& ability) const;

	void calculate_magnitudes(const Ref<ActiveGameplayEffect>& active);
	void apply_modifiers_to_base(const Ref<ActiveGameplayEffect>& active);
	void execute_periodic(const Ref<ActiveGameplayEffect>& active);
	void grant_effect_tags(const Ref<GameplayEffect>& effect, int direction);
	void fire_effect_cues(const Ref<ActiveGameplayEffect>& active, CueEvent event);
	void run_cue_handler(const StringName& cue_tag_name, CueEvent event, const Dictionary& parameters);
	void remove_active_effect_internal(int index);
	void recompute_all_attributes();
	int find_active_effect_index(int64_t active_id) const;

	void emit_cue(const StringName& cue_tag_name, CueEvent event, const Dictionary& parameters);

	// --- Replication helpers ---
	void queue_call(int target, const StringName& method, const Array& arguments);
	void queue_flush();
	void flush_replication();
	PackedInt32Array resolve_targets(int target) const;
	void configure_rpcs();
	bool is_sender_owner() const;

	Dictionary serialize_effect(const Ref<ActiveGameplayEffect>& active) const;
	Dictionary serialize_ability(const Ref<GameplayAbility>& ability) const;
	Dictionary build_full_state(int for_peer) const;

	Ref<ActiveGameplayEffect> build_mirror_effect(const Dictionary& data) const;
	void apply_replicated_tag_count(const StringName& tag_name, int count);
};

}

VARIANT_ENUM_CAST(GFGD::AbilitySystemComponent::ReplicationMode);
VARIANT_ENUM_CAST(GFGD::AbilitySystemComponent::CueEvent);

#endif
