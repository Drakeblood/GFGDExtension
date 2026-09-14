#ifndef GAMEPLAY_ABILITY_H
#define GAMEPLAY_ABILITY_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

#include "gameplay_tags/gameplay_tag_container.h"
#include "ability_system/ability_task.h"

using namespace godot;

namespace GFGD
{
class AbilitySystemComponent;
class GameplayEffect;
class GameplayEffectSpec;

class GameplayAbility : public Resource
{
	GDCLASS(GameplayAbility, Resource)

public:
	// Where an ability runs when a networked game activates it. Only matters on
	// a replicated AbilitySystemComponent; offline every ability is local.
	enum NetExecutionPolicy
	{
		// Only on the machine that activated it. The server never hears of it.
		LOCAL_ONLY = 0,

		// The owning client runs it at once and asks the server, which runs it
		// too and confirms - or refuses, and the client's copy is cancelled.
		// For anything the player should feel without a round trip.
		LOCAL_PREDICTED = 1,

		// The owning client only asks; the server runs it, and the client learns
		// that it is active. For anything a client must not be trusted with.
		SERVER_ONLY = 2,
	};

private:
	AbilitySystemComponent* ability_system_component;
	StringName ability_name;
	int64_t ability_id;
	static int64_t id_counter;
	bool is_active;
	bool is_input_pressed;
	Variant source_object;

	Ref<GameplayTagContainer> ability_tags;
	Ref<GameplayTagContainer> cancel_abilities_with_tag;
	Ref<GameplayTagContainer> block_abilities_with_tag;
	Ref<GameplayTagContainer> activation_owned_tags;
	Ref<GameplayTagContainer> activation_required_tags;
	Ref<GameplayTagContainer> activation_blocked_tags;

	StringName input_action_name;

	NetExecutionPolicy net_execution_policy;

	// Instant, normally a negative ADD on some resource attribute. Checked by
	// can_activate_ability, applied by commit_ability.
	Ref<GameplayEffect> cost_effect;

	// Has a duration; its granted_tags are the cooldown. The ability cannot
	// activate while the owner has any of them.
	Ref<GameplayEffect> cooldown_effect;

	// Gameplay events that activate this ability by themselves - Unreal's
	// AbilityTriggers with a GameplayEvent source. "Event" is triggered by
	// "Event.Hit" too.
	Ref<GameplayTagContainer> trigger_event_tags;

	// The event that started the current activation, if one did.
	StringName trigger_event_tag;
	Dictionary trigger_event_data;

	float ability_level;

	// The id the server gave this ability when it was granted, the same on the
	// server and on the owning client - unlike ability_id, which is per machine.
	int64_t handle;

	// A client's copy running ahead of the server's answer.
	bool predicting;
	int64_t prediction_key;

	StringName last_failure_reason;

	// The tasks this activation is running; cancelled, all of them, when it ends.
	Vector<Ref<AbilityTask>> active_tasks;
	uint64_t activation_time_usec;

	// sync_target_data calls are numbered in order within an activation, the
	// same on both sides, so the server matches each value to the call it
	// answers.
	int next_sync_sequence;
	Dictionary received_target_data;

	// Persistent cues this activation added, removed when it ends - Unreal's
	// bRemoveOnAbilityEnd, which is on by default there too.
	PackedStringArray added_cues;

public:
	GameplayAbility();
	~GameplayAbility();

	static int64_t get_id_counter();
	static void increment_id_counter();

	StringName get_ability_name() const { return ability_name; }
	void set_ability_name(const StringName& value) { ability_name = value; }

	int64_t get_ability_id() const { return ability_id; }
	bool get_is_active() const { return is_active; }
	bool get_is_input_pressed() const { return is_input_pressed; }
	void set_is_input_pressed(bool value) { is_input_pressed = value; }
	Variant get_source_object() const { return source_object; }

	AbilitySystemComponent* get_ability_system_component() const { return ability_system_component; }

	Ref<GameplayTagContainer> get_ability_tags() const { return ability_tags; }
	void set_ability_tags(const Ref<GameplayTagContainer>& value) { ability_tags = value; }
	Ref<GameplayTagContainer> get_cancel_abilities_with_tag() const { return cancel_abilities_with_tag; }
	void set_cancel_abilities_with_tag(const Ref<GameplayTagContainer>& value) { cancel_abilities_with_tag = value; }
	Ref<GameplayTagContainer> get_block_abilities_with_tag() const { return block_abilities_with_tag; }
	void set_block_abilities_with_tag(const Ref<GameplayTagContainer>& value) { block_abilities_with_tag = value; }
	Ref<GameplayTagContainer> get_activation_owned_tags() const { return activation_owned_tags; }
	void set_activation_owned_tags(const Ref<GameplayTagContainer>& value) { activation_owned_tags = value; }
	Ref<GameplayTagContainer> get_activation_required_tags() const { return activation_required_tags; }
	void set_activation_required_tags(const Ref<GameplayTagContainer>& value) { activation_required_tags = value; }
	Ref<GameplayTagContainer> get_activation_blocked_tags() const { return activation_blocked_tags; }
	void set_activation_blocked_tags(const Ref<GameplayTagContainer>& value) { activation_blocked_tags = value; }

	StringName get_input_action_name() const { return input_action_name; }
	void set_input_action_name(const StringName& value) { input_action_name = value; }

	NetExecutionPolicy get_net_execution_policy() const { return net_execution_policy; }
	void set_net_execution_policy(NetExecutionPolicy value) { net_execution_policy = value; }

	Ref<GameplayEffect> get_cost_effect() const { return cost_effect; }
	void set_cost_effect(const Ref<GameplayEffect>& value) { cost_effect = value; }

	Ref<GameplayEffect> get_cooldown_effect() const { return cooldown_effect; }
	void set_cooldown_effect(const Ref<GameplayEffect>& value) { cooldown_effect = value; }

	Ref<GameplayTagContainer> get_trigger_event_tags() const { return trigger_event_tags; }
	void set_trigger_event_tags(const Ref<GameplayTagContainer>& value) { trigger_event_tags = value; }

	// Whether an event with this tag is one of this ability's triggers.
	bool is_triggered_by(const StringName& event_tag) const;

	// Asks the script, when it overrides _should_respond_to_event; yes otherwise.
	bool should_respond_to_event(const StringName& event_tag, const Dictionary& payload);

	// What started the current activation: the event's tag and payload, or empty
	// for an activation nobody triggered. On the server's copy of a predicted
	// activation, the payload is the one the client sent - untrusted.
	StringName get_trigger_event_tag() const { return trigger_event_tag; }
	Dictionary get_trigger_event_data() const { return trigger_event_data; }
	void set_trigger_event(const StringName& tag, const Dictionary& data)
	{
		trigger_event_tag = tag;
		trigger_event_data = data;
	}

	float get_ability_level() const { return ability_level; }
	void set_ability_level(float value) { ability_level = value; }

	int64_t get_handle() const { return handle; }
	void set_handle(int64_t value) { handle = value; }

	bool is_predicting() const { return predicting; }
	void set_predicting(bool value) { predicting = value; }
	int64_t get_prediction_key() const { return prediction_key; }
	void set_prediction_key(int64_t value) { prediction_key = value; }

	// Why the last can_activate_ability said no: &"active", &"no_owner",
	// &"blocked", &"tags", &"cooldown", &"cost", &"script", or &"rejected" when
	// the server refused a predicted activation. Empty after a yes.
	StringName get_last_failure_reason() const { return last_failure_reason; }
	void set_last_failure_reason(const StringName& value) { last_failure_reason = value; }

	// The node the ability system component hangs off - the pawn root, for a
	// component placed the usual way.
	Node* get_avatar() const;

	// --- Cost and cooldown ---

	bool check_cost() const;
	bool check_cooldown() const;

	// Checks cost and cooldown again and, where effects may be applied, applies
	// both. Call it at the point the ability has really happened - after the
	// wind-up, not before it - and end the ability if it returns false.
	bool commit_ability();
	bool commit_ability_cost();
	bool commit_ability_cooldown();

	Ref<GameplayTagContainer> get_cooldown_tags() const;
	double get_cooldown_time_remaining() const;

	// --- Effects from the ability ---

	// A spec at this ability's level, with the owner as its source.
	Ref<GameplayEffectSpec> make_outgoing_spec(const Ref<GameplayEffect>& effect) const;
	int64_t apply_effect_spec_to_owner(const Ref<GameplayEffectSpec>& spec);
	int64_t apply_effect_to_target(const Ref<GameplayEffect>& effect, AbilitySystemComponent* target);

	// --- Tasks: things an activation waits for ---
	//
	// Each returns a task to `await task.completed` on. Every one of them is
	// cancelled when the ability ends, and a cancelled task never resumes the
	// code awaiting it.

	Ref<AbilityTask> wait_delay(double seconds);
	Ref<AbilityTask> wait_input_press();
	Ref<AbilityTask> wait_input_release();
	Ref<AbilityTask> wait_gameplay_tag_added(const StringName& tag_name);
	Ref<AbilityTask> wait_gameplay_tag_removed(const StringName& tag_name);
	Ref<AbilityTask> wait_attribute_change(const StringName& attribute_name);
	Ref<AbilityTask> wait_gameplay_event(const StringName& event_tag);
	Ref<AbilityTask> wait_any(const Array& tasks);
	Ref<AbilityTask> wait_all(const Array& tasks);

	// On the predicting client: sends data to the server and completes with it
	// at once. On the server's copy of that activation: completes with the
	// client's data, whatever was passed here. Anywhere else: completes with
	// data. What arrives from a client is untrusted - check it before using it.
	Ref<AbilityTask> sync_target_data(const Dictionary& data);

	// Starts a task of your own - an AbilityTask subclass - for this activation.
	Ref<AbilityTask> run_task(const Ref<AbilityTask>& task);

	// --- Gameplay cues from an ability ---
	//
	// Use these rather than the component's own from inside an ability. On the
	// server's copy of a predicted activation they leave the owning client out
	// of what is sent, because that client played the cue itself when it
	// predicted - through the component directly, it would play twice. They also
	// add the avatar as source_path, and a cue added here is removed when the
	// ability ends.
	void execute_gameplay_cue(const StringName& cue_tag, const Dictionary& parameters = Dictionary());
	void add_gameplay_cue(const StringName& cue_tag, const Dictionary& parameters = Dictionary());
	void remove_gameplay_cue(const StringName& cue_tag);

	Array get_active_tasks() const;
	double get_time_active() const;

	// Internal, for the tasks and the component.
	void unregister_task(AbilityTask* task);
	void tick_tasks(double delta);
	void cancel_all_tasks();
	void receive_target_data(int64_t key, int sequence, const Dictionary& data);
	bool take_received_target_data(int sequence, Variant& out_data);

	// Called by the component when the other side of the network says the
	// ability started or stopped. No checks, no request sent back.
	void activate_from_remote();
	void end_from_remote(bool was_canceled);

	void setup_ability(AbilitySystemComponent* ability_system_component, const Variant& source_object = Variant());
	void on_give_ability();
	void on_remove_ability();

	bool can_activate_ability();
	void activate_ability();
	void end_ability(bool was_canceled = false);

	void input_pressed();
	void input_released();

	int64_t apply_effect_to_owner(const Ref<GameplayEffect>& effect);

	GDVIRTUAL0(_on_give_ability)
	GDVIRTUAL0R(bool, _can_activate_ability)
	GDVIRTUAL0(_activate_ability)
	GDVIRTUAL1(_end_ability, bool)
	GDVIRTUAL0(_input_pressed)
	GDVIRTUAL0(_input_released)
	GDVIRTUAL0(_on_remove_ability)
	GDVIRTUAL2R(bool, _should_respond_to_event, StringName, Dictionary)

protected:
	static void _bind_methods();

private:
	void apply_activation_tags(int direction);
	void run_activation();
	void run_end(bool was_canceled);
};

}

VARIANT_ENUM_CAST(GFGD::GameplayAbility::NetExecutionPolicy);

#endif
