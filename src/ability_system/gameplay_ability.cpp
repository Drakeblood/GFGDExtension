#include "ability_system/gameplay_ability.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/time.hpp>

#include "ability_system/ability_system_component.h"
#include "gameplay_tags/gameplay_tag_container.h"
#include "gameplay_tags/gameplay_tag.h"
#include "ability_system/gameplay_effect.h"
#include "ability_system/gameplay_effect_spec.h"
#include "ability_system/attribute_modifier.h"

using namespace godot;

namespace GFGD
{
int64_t GameplayAbility::id_counter = 0;

GameplayAbility::GameplayAbility()
{
	ability_system_component = nullptr;
	ability_id = -1;
	is_active = false;
	is_input_pressed = false;
	net_execution_policy = LOCAL_PREDICTED;
	ability_level = 1.0f;
	handle = 0;
	predicting = false;
	prediction_key = 0;
	activation_time_usec = 0;
	next_sync_sequence = 0;

	// The tag containers stay null. ClassDB snapshots a freshly constructed
	// instance to learn each property's default value, and warns about every
	// live object it finds there. A null container reads the same as an empty
	// one everywhere - has_any/has_all take a null Ref - and the inspector
	// creates one on the first edit.
}

GameplayAbility::~GameplayAbility()
{

}

int64_t GameplayAbility::get_id_counter()
{
	return id_counter;
}

void GameplayAbility::increment_id_counter()
{
	id_counter++;
}

void GameplayAbility::setup_ability(AbilitySystemComponent* in_ability_system_component, const Variant& in_source_object)
{
	ability_system_component = in_ability_system_component;
	source_object = in_source_object;

	ability_id = id_counter;
	increment_id_counter();
}

void GameplayAbility::on_give_ability()
{
	GDVIRTUAL_CALL(_on_give_ability);
}

void GameplayAbility::on_remove_ability()
{
	GDVIRTUAL_CALL(_on_remove_ability);
}

Node* GameplayAbility::get_avatar() const
{
	return ability_system_component != nullptr ? ability_system_component->get_parent() : nullptr;
}

bool GameplayAbility::can_activate_ability()
{
	last_failure_reason = StringName();

	if (get_is_active()) { last_failure_reason = "active"; return false; }
	if (!ability_system_component) { last_failure_reason = "no_owner"; return false; }

	Ref<GameplayTagContainer> blocked_tags = ability_system_component->get_blocked_ability_tags();
	if (blocked_tags.is_valid() && blocked_tags->has_any(ability_tags)) { last_failure_reason = "blocked"; return false; }

	Ref<GameplayTagContainer> owned_tags = ability_system_component->get_owned_gameplay_tags();
	if (owned_tags.is_valid())
	{
		if (owned_tags->has_any(activation_blocked_tags)) { last_failure_reason = "tags"; return false; }
		if (!owned_tags->has_all(activation_required_tags)) { last_failure_reason = "tags"; return false; }
	}

	if (!check_cooldown()) { last_failure_reason = "cooldown"; return false; }
	if (!check_cost()) { last_failure_reason = "cost"; return false; }

	bool script_allows = true;
	if (GDVIRTUAL_CALL(_can_activate_ability, script_allows))
	{
		if (!script_allows) { last_failure_reason = "script"; return false; }
	}

	return true;
}

void GameplayAbility::activate_ability()
{
	if (!ability_system_component || is_active) { return; }
	run_activation();
}

void GameplayAbility::activate_from_remote()
{
	if (!ability_system_component || is_active) { return; }
	run_activation();
}

void GameplayAbility::run_activation()
{
	is_active = true;
	activation_time_usec = Time::get_singleton()->get_ticks_usec();
	next_sync_sequence = 0;

	// A server-only ability on a client is a mirror: it is marked active so a
	// UI can show it, but its script and its tags run on the server alone - the
	// tags reach this machine through the owned-tag replication.
	const bool runs_here = ability_system_component->runs_ability_logic(Ref<GameplayAbility>(this));

	if (runs_here)
	{
		apply_activation_tags(1);

		if (cancel_abilities_with_tag.is_valid() && cancel_abilities_with_tag->get_length() > 0)
		{
			ability_system_component->cancel_abilities_with_tags(cancel_abilities_with_tag);
		}
	}

	ability_system_component->notify_ability_activated(Ref<GameplayAbility>(this));

	if (runs_here)
	{
		GDVIRTUAL_CALL(_activate_ability);
	}
}

void GameplayAbility::end_ability(bool was_canceled)
{
	if (!ability_system_component || !is_active) { return; }
	run_end(was_canceled);
}

void GameplayAbility::end_from_remote(bool was_canceled)
{
	if (!ability_system_component || !is_active) { return; }
	run_end(was_canceled);
}

void GameplayAbility::run_end(bool was_canceled)
{
	is_active = false;
	predicting = false;

	// First, so nothing an ending ability was waiting for resumes during or after
	// its own end.
	cancel_all_tasks();
	received_target_data.clear();

	const PackedStringArray cues = added_cues;
	added_cues.clear();
	for (int i = 0; i < cues.size(); i++)
	{
		remove_gameplay_cue(cues[i]);
	}

	const bool runs_here = ability_system_component->runs_ability_logic(Ref<GameplayAbility>(this));

	if (runs_here)
	{
		GDVIRTUAL_CALL(_end_ability, was_canceled);
		apply_activation_tags(-1);
	}

	emit_signal("ability_ended", was_canceled);
	ability_system_component->notify_ability_ended(Ref<GameplayAbility>(this), was_canceled);
}

bool GameplayAbility::is_triggered_by(const StringName& event_tag) const
{
	if (trigger_event_tags.is_null() || event_tag == StringName()) { return false; }

	Ref<GameplayTag> event;
	event.instantiate();
	event->set_tag_name(event_tag);

	for (int i = 0; i < trigger_event_tags->get_length(); i++)
	{
		// The event, or any child of it, matches a trigger - "Event.Hit.Head"
		// fires an ability triggered by "Event.Hit".
		if (event->matches_tag(trigger_event_tags->get_tag(i))) { return true; }
	}
	return false;
}

bool GameplayAbility::should_respond_to_event(const StringName& event_tag, const Dictionary& payload)
{
	bool respond = true;
	GDVIRTUAL_CALL(_should_respond_to_event, event_tag, payload, respond);
	return respond;
}

// --- Cost and cooldown ---

bool GameplayAbility::check_cost() const
{
	if (cost_effect.is_null() || ability_system_component == nullptr) { return true; }

	const Ref<GameplayEffectSpec> spec = make_outgoing_spec(cost_effect);
	const TypedArray<AttributeModifier> modifiers = cost_effect->get_modifiers();

	// The cost is affordable if applying it would leave nothing below zero -
	// Unreal's CanApplyAttributeModifiers. A cost of 20 stamina with 19 left is
	// refused; with exactly 20 it goes through and leaves 0.
	for (int i = 0; i < modifiers.size(); i++)
	{
		const Ref<AttributeModifier> modifier = modifiers[i];
		if (modifier.is_null() || modifier->get_attribute() == StringName()) { continue; }

		const double magnitude = modifier->calculate_magnitude(spec, ability_system_component);
		const double current = ability_system_component->get_attribute_value(modifier->get_attribute());
		if (modifier->apply_magnitude(current, magnitude) < 0.0)
		{
			return false;
		}
	}

	return true;
}

bool GameplayAbility::check_cooldown() const
{
	const Ref<GameplayTagContainer> cooldown_tags = get_cooldown_tags();
	if (cooldown_tags.is_null() || cooldown_tags->get_length() == 0 || ability_system_component == nullptr) { return true; }

	const Ref<GameplayTagContainer> owned = ability_system_component->get_owned_gameplay_tags();
	return owned.is_null() || !owned->has_any(cooldown_tags);
}

bool GameplayAbility::commit_ability()
{
	if (!check_cooldown()) { last_failure_reason = "cooldown"; return false; }
	if (!check_cost()) { last_failure_reason = "cost"; return false; }

	commit_ability_cost();
	commit_ability_cooldown();
	return true;
}

bool GameplayAbility::commit_ability_cost()
{
	if (!check_cost()) { last_failure_reason = "cost"; return false; }
	if (cost_effect.is_valid() && ability_system_component->can_apply_effects())
	{
		ability_system_component->apply_gameplay_effect_spec_to_self(make_outgoing_spec(cost_effect));
	}
	return true;
}

bool GameplayAbility::commit_ability_cooldown()
{
	if (!check_cooldown()) { last_failure_reason = "cooldown"; return false; }
	if (cooldown_effect.is_valid() && ability_system_component->can_apply_effects())
	{
		ability_system_component->apply_gameplay_effect_spec_to_self(make_outgoing_spec(cooldown_effect));
	}
	return true;
}

Ref<GameplayTagContainer> GameplayAbility::get_cooldown_tags() const
{
	return cooldown_effect.is_valid() ? cooldown_effect->get_granted_tags() : Ref<GameplayTagContainer>();
}

double GameplayAbility::get_cooldown_time_remaining() const
{
	if (ability_system_component == nullptr) { return 0.0; }
	return ability_system_component->get_effects_time_remaining_with_granted_tags(get_cooldown_tags());
}

// --- Effects from the ability ---

Ref<GameplayEffectSpec> GameplayAbility::make_outgoing_spec(const Ref<GameplayEffect>& effect) const
{
	Ref<GameplayEffectSpec> spec;
	spec.instantiate();
	spec->set_effect(effect);
	spec->set_level(ability_level);
	spec->set_source(ability_system_component);
	return spec;
}

int64_t GameplayAbility::apply_effect_spec_to_owner(const Ref<GameplayEffectSpec>& spec)
{
	if (!ability_system_component) { return -1; }
	return ability_system_component->apply_gameplay_effect_spec_to_self(spec);
}

int64_t GameplayAbility::apply_effect_to_target(const Ref<GameplayEffect>& effect, AbilitySystemComponent* target)
{
	if (!ability_system_component || target == nullptr) { return -1; }
	return ability_system_component->apply_gameplay_effect_spec_to_target(make_outgoing_spec(effect), target);
}

void GameplayAbility::input_pressed()
{
	GDVIRTUAL_CALL(_input_pressed);

	const Vector<Ref<AbilityTask>> tasks = active_tasks;
	for (int i = 0; i < tasks.size(); i++)
	{
		if (AbilityTaskWaitInput* wait = Object::cast_to<AbilityTaskWaitInput>(tasks[i].ptr()))
		{
			wait->notify_input(true);
		}
	}
}

void GameplayAbility::input_released()
{
	GDVIRTUAL_CALL(_input_released);

	const Vector<Ref<AbilityTask>> tasks = active_tasks;
	for (int i = 0; i < tasks.size(); i++)
	{
		if (AbilityTaskWaitInput* wait = Object::cast_to<AbilityTaskWaitInput>(tasks[i].ptr()))
		{
			wait->notify_input(false);
		}
	}
}

// --- Gameplay cues ---

static Dictionary with_source(const Dictionary& parameters, AbilitySystemComponent* asc)
{
	Dictionary result = parameters.duplicate();
	if (!result.has("source_path") && asc != nullptr && asc->is_inside_tree())
	{
		Node* avatar = asc->get_parent() != nullptr ? asc->get_parent() : asc;
		result["source_path"] = avatar->get_path();
	}
	return result;
}

void GameplayAbility::execute_gameplay_cue(const StringName& cue_tag, const Dictionary& parameters)
{
	if (ability_system_component == nullptr) { return; }
	ability_system_component->execute_gameplay_cue_by_name(cue_tag, with_source(parameters, ability_system_component),
			ability_system_component->expects_client_target_data(Ref<GameplayAbility>(this)));
}

void GameplayAbility::add_gameplay_cue(const StringName& cue_tag, const Dictionary& parameters)
{
	if (ability_system_component == nullptr) { return; }
	ability_system_component->add_gameplay_cue_by_name(cue_tag, with_source(parameters, ability_system_component),
			ability_system_component->expects_client_target_data(Ref<GameplayAbility>(this)));
	added_cues.push_back(cue_tag);
}

void GameplayAbility::remove_gameplay_cue(const StringName& cue_tag)
{
	if (ability_system_component == nullptr) { return; }

	const int64_t index = added_cues.find(cue_tag);
	if (index >= 0)
	{
		added_cues.remove_at(index);
	}
	ability_system_component->remove_gameplay_cue_by_name(cue_tag, ability_system_component->expects_client_target_data(Ref<GameplayAbility>(this)));
}

// --- Tasks ---

Ref<AbilityTask> GameplayAbility::run_task(const Ref<AbilityTask>& task)
{
	ERR_FAIL_COND_V_MSG(task.is_null(), task, "GFGD: run_task was given no task.");

	if (!is_active)
	{
		// Nothing would ever cancel it. Handed back cancelled instead, so an
		// await on it is released rather than left hanging.
		WARN_PRINT(vformat("GFGD: ability '%s' started a task while not active; it was cancelled at once.", String(ability_name)));
		task->cancel();
		return task;
	}

	active_tasks.push_back(task);
	task->start(this);
	return task;
}

Ref<AbilityTask> GameplayAbility::wait_delay(double seconds)
{
	Ref<AbilityTaskWaitDelay> task;
	task.instantiate();
	task->setup(seconds);
	return run_task(task);
}

Ref<AbilityTask> GameplayAbility::wait_input_press()
{
	Ref<AbilityTaskWaitInput> task;
	task.instantiate();
	task->setup(true);
	return run_task(task);
}

Ref<AbilityTask> GameplayAbility::wait_input_release()
{
	Ref<AbilityTaskWaitInput> task;
	task.instantiate();
	task->setup(false);
	return run_task(task);
}

Ref<AbilityTask> GameplayAbility::wait_gameplay_tag_added(const StringName& tag_name)
{
	Ref<AbilityTaskWaitTag> task;
	task.instantiate();
	task->setup(tag_name, true);
	return run_task(task);
}

Ref<AbilityTask> GameplayAbility::wait_gameplay_tag_removed(const StringName& tag_name)
{
	Ref<AbilityTaskWaitTag> task;
	task.instantiate();
	task->setup(tag_name, false);
	return run_task(task);
}

Ref<AbilityTask> GameplayAbility::wait_attribute_change(const StringName& attribute_name)
{
	Ref<AbilityTaskWaitAttribute> task;
	task.instantiate();
	task->setup(attribute_name);
	return run_task(task);
}

Ref<AbilityTask> GameplayAbility::wait_gameplay_event(const StringName& event_tag)
{
	Ref<AbilityTaskWaitEvent> task;
	task.instantiate();
	task->setup(event_tag);
	return run_task(task);
}

Ref<AbilityTask> GameplayAbility::wait_any(const Array& tasks)
{
	Ref<AbilityTaskWaitGroup> task;
	task.instantiate();
	task->setup(tasks, false);
	return run_task(task);
}

Ref<AbilityTask> GameplayAbility::wait_all(const Array& tasks)
{
	Ref<AbilityTaskWaitGroup> task;
	task.instantiate();
	task->setup(tasks, true);
	return run_task(task);
}

Ref<AbilityTask> GameplayAbility::sync_target_data(const Dictionary& data)
{
	Ref<AbilityTaskSyncData> task;
	task.instantiate();
	task->setup(data, next_sync_sequence++);
	return run_task(task);
}

Array GameplayAbility::get_active_tasks() const
{
	Array result;
	for (int i = 0; i < active_tasks.size(); i++)
	{
		result.push_back(active_tasks[i]);
	}
	return result;
}

double GameplayAbility::get_time_active() const
{
	if (!is_active) { return 0.0; }
	return (double)(Time::get_singleton()->get_ticks_usec() - activation_time_usec) / 1000000.0;
}

void GameplayAbility::unregister_task(AbilityTask* task)
{
	for (int i = 0; i < active_tasks.size(); i++)
	{
		if (active_tasks[i].ptr() == task)
		{
			active_tasks.remove_at(i);
			return;
		}
	}
}

void GameplayAbility::tick_tasks(double delta)
{
	if (active_tasks.is_empty()) { return; }

	// A copy: a task finishing resumes a coroutine, which may start tasks or end
	// the ability - both change the list being walked.
	const Vector<Ref<AbilityTask>> tasks = active_tasks;
	for (int i = 0; i < tasks.size(); i++)
	{
		tasks[i]->tick(delta);
	}
}

void GameplayAbility::cancel_all_tasks()
{
	const Vector<Ref<AbilityTask>> tasks = active_tasks;
	active_tasks.clear();
	for (int i = 0; i < tasks.size(); i++)
	{
		tasks[i]->cancel();
	}
}

void GameplayAbility::receive_target_data(int64_t key, int sequence, const Dictionary& data)
{
	// For another activation - an older one, or one the server refused.
	if (!is_active || key != prediction_key) { return; }

	const Vector<Ref<AbilityTask>> tasks = active_tasks;
	for (int i = 0; i < tasks.size(); i++)
	{
		AbilityTaskSyncData* sync = Object::cast_to<AbilityTaskSyncData>(tasks[i].ptr());
		if (sync != nullptr && sync->get_sequence() == sequence && sync->is_running())
		{
			sync->finish(data);
			return;
		}
	}

	// Arrived before the server's copy got that far; the task takes it when it
	// starts.
	received_target_data[sequence] = data;
}

bool GameplayAbility::take_received_target_data(int sequence, Variant& out_data)
{
	if (!received_target_data.has(sequence)) { return false; }
	out_data = received_target_data[sequence];
	received_target_data.erase(sequence);
	return true;
}

int64_t GameplayAbility::apply_effect_to_owner(const Ref<GameplayEffect>& effect)
{
	if (!ability_system_component) { return -1; }
	return ability_system_component->apply_gameplay_effect_spec_to_self(make_outgoing_spec(effect));
}

void GameplayAbility::apply_activation_tags(int direction)
{
	if (activation_owned_tags.is_valid())
	{
		for (int i = 0; i < activation_owned_tags->get_length(); i++)
		{
			Ref<GameplayTag> tag = activation_owned_tags->get_tag(i);
			if (tag.is_valid())
			{
				ability_system_component->update_tag_map(tag, direction);
			}
		}
	}

	if (block_abilities_with_tag.is_valid())
	{
		for (int i = 0; i < block_abilities_with_tag->get_length(); i++)
		{
			Ref<GameplayTag> tag = block_abilities_with_tag->get_tag(i);
			if (tag.is_valid())
			{
				ability_system_component->update_blocked_ability_tags(tag, direction);
			}
		}
	}
}

void GameplayAbility::_bind_methods()
{
	ClassDB::bind_static_method("GameplayAbility", D_METHOD("get_id_counter"), &GameplayAbility::get_id_counter);
	ClassDB::bind_static_method("GameplayAbility", D_METHOD("increment_id_counter"), &GameplayAbility::increment_id_counter);

	ClassDB::bind_method(D_METHOD("get_ability_name"), &GameplayAbility::get_ability_name);
	ClassDB::bind_method(D_METHOD("set_ability_name", "value"), &GameplayAbility::set_ability_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "ability_name"), "set_ability_name", "get_ability_name");

	ClassDB::bind_method(D_METHOD("get_ability_id"), &GameplayAbility::get_ability_id);
	ClassDB::bind_method(D_METHOD("get_is_active"), &GameplayAbility::get_is_active);
	ClassDB::bind_method(D_METHOD("get_is_input_pressed"), &GameplayAbility::get_is_input_pressed);
	ClassDB::bind_method(D_METHOD("set_is_input_pressed", "value"), &GameplayAbility::set_is_input_pressed);
	ClassDB::bind_method(D_METHOD("get_source_object"), &GameplayAbility::get_source_object);
	ClassDB::bind_method(D_METHOD("get_ability_system_component"), &GameplayAbility::get_ability_system_component);

	ClassDB::bind_method(D_METHOD("get_ability_tags"), &GameplayAbility::get_ability_tags);
	ClassDB::bind_method(D_METHOD("set_ability_tags", "value"), &GameplayAbility::set_ability_tags);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "ability_tags", PROPERTY_HINT_RESOURCE_TYPE, "GameplayTagContainer"), "set_ability_tags", "get_ability_tags");

	ClassDB::bind_method(D_METHOD("get_cancel_abilities_with_tag"), &GameplayAbility::get_cancel_abilities_with_tag);
	ClassDB::bind_method(D_METHOD("set_cancel_abilities_with_tag", "value"), &GameplayAbility::set_cancel_abilities_with_tag);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "cancel_abilities_with_tag", PROPERTY_HINT_RESOURCE_TYPE, "GameplayTagContainer"), "set_cancel_abilities_with_tag", "get_cancel_abilities_with_tag");

	ClassDB::bind_method(D_METHOD("get_block_abilities_with_tag"), &GameplayAbility::get_block_abilities_with_tag);
	ClassDB::bind_method(D_METHOD("set_block_abilities_with_tag", "value"), &GameplayAbility::set_block_abilities_with_tag);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "block_abilities_with_tag", PROPERTY_HINT_RESOURCE_TYPE, "GameplayTagContainer"), "set_block_abilities_with_tag", "get_block_abilities_with_tag");

	ClassDB::bind_method(D_METHOD("get_activation_owned_tags"), &GameplayAbility::get_activation_owned_tags);
	ClassDB::bind_method(D_METHOD("set_activation_owned_tags", "value"), &GameplayAbility::set_activation_owned_tags);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "activation_owned_tags", PROPERTY_HINT_RESOURCE_TYPE, "GameplayTagContainer"), "set_activation_owned_tags", "get_activation_owned_tags");

	ClassDB::bind_method(D_METHOD("get_activation_required_tags"), &GameplayAbility::get_activation_required_tags);
	ClassDB::bind_method(D_METHOD("set_activation_required_tags", "value"), &GameplayAbility::set_activation_required_tags);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "activation_required_tags", PROPERTY_HINT_RESOURCE_TYPE, "GameplayTagContainer"), "set_activation_required_tags", "get_activation_required_tags");

	ClassDB::bind_method(D_METHOD("get_activation_blocked_tags"), &GameplayAbility::get_activation_blocked_tags);
	ClassDB::bind_method(D_METHOD("set_activation_blocked_tags", "value"), &GameplayAbility::set_activation_blocked_tags);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "activation_blocked_tags", PROPERTY_HINT_RESOURCE_TYPE, "GameplayTagContainer"), "set_activation_blocked_tags", "get_activation_blocked_tags");

	ClassDB::bind_method(D_METHOD("get_input_action_name"), &GameplayAbility::get_input_action_name);
	ClassDB::bind_method(D_METHOD("set_input_action_name", "value"), &GameplayAbility::set_input_action_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "input_action_name"), "set_input_action_name", "get_input_action_name");

	ClassDB::bind_method(D_METHOD("setup_ability", "ability_system_component", "source_object"), &GameplayAbility::setup_ability, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("on_give_ability"), &GameplayAbility::on_give_ability);

	ClassDB::bind_method(D_METHOD("can_activate_ability"), &GameplayAbility::can_activate_ability);
	ClassDB::bind_method(D_METHOD("activate_ability"), &GameplayAbility::activate_ability);
	ClassDB::bind_method(D_METHOD("end_ability", "was_canceled"), &GameplayAbility::end_ability, DEFVAL(false));

	ClassDB::bind_method(D_METHOD("input_pressed"), &GameplayAbility::input_pressed);
	ClassDB::bind_method(D_METHOD("input_released"), &GameplayAbility::input_released);

	ClassDB::bind_method(D_METHOD("apply_effect_to_owner", "effect"), &GameplayAbility::apply_effect_to_owner);
	ClassDB::bind_method(D_METHOD("apply_effect_spec_to_owner", "spec"), &GameplayAbility::apply_effect_spec_to_owner);
	ClassDB::bind_method(D_METHOD("apply_effect_to_target", "effect", "target"), &GameplayAbility::apply_effect_to_target);
	ClassDB::bind_method(D_METHOD("make_outgoing_spec", "effect"), &GameplayAbility::make_outgoing_spec);

	ClassDB::bind_method(D_METHOD("get_avatar"), &GameplayAbility::get_avatar);

	ClassDB::bind_method(D_METHOD("wait_delay", "seconds"), &GameplayAbility::wait_delay);
	ClassDB::bind_method(D_METHOD("wait_input_press"), &GameplayAbility::wait_input_press);
	ClassDB::bind_method(D_METHOD("wait_input_release"), &GameplayAbility::wait_input_release);
	ClassDB::bind_method(D_METHOD("wait_gameplay_tag_added", "tag_name"), &GameplayAbility::wait_gameplay_tag_added);
	ClassDB::bind_method(D_METHOD("wait_gameplay_tag_removed", "tag_name"), &GameplayAbility::wait_gameplay_tag_removed);
	ClassDB::bind_method(D_METHOD("wait_attribute_change", "attribute_name"), &GameplayAbility::wait_attribute_change);
	ClassDB::bind_method(D_METHOD("wait_gameplay_event", "event_tag"), &GameplayAbility::wait_gameplay_event);
	ClassDB::bind_method(D_METHOD("wait_any", "tasks"), &GameplayAbility::wait_any);
	ClassDB::bind_method(D_METHOD("wait_all", "tasks"), &GameplayAbility::wait_all);
	ClassDB::bind_method(D_METHOD("sync_target_data", "data"), &GameplayAbility::sync_target_data);
	ClassDB::bind_method(D_METHOD("run_task", "task"), &GameplayAbility::run_task);
	ClassDB::bind_method(D_METHOD("execute_gameplay_cue", "cue_tag", "parameters"), &GameplayAbility::execute_gameplay_cue, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("add_gameplay_cue", "cue_tag", "parameters"), &GameplayAbility::add_gameplay_cue, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("remove_gameplay_cue", "cue_tag"), &GameplayAbility::remove_gameplay_cue);
	ClassDB::bind_method(D_METHOD("get_active_tasks"), &GameplayAbility::get_active_tasks);
	ClassDB::bind_method(D_METHOD("get_time_active"), &GameplayAbility::get_time_active);
	ClassDB::bind_method(D_METHOD("check_cost"), &GameplayAbility::check_cost);
	ClassDB::bind_method(D_METHOD("check_cooldown"), &GameplayAbility::check_cooldown);
	ClassDB::bind_method(D_METHOD("commit_ability"), &GameplayAbility::commit_ability);
	ClassDB::bind_method(D_METHOD("commit_ability_cost"), &GameplayAbility::commit_ability_cost);
	ClassDB::bind_method(D_METHOD("commit_ability_cooldown"), &GameplayAbility::commit_ability_cooldown);
	ClassDB::bind_method(D_METHOD("get_cooldown_tags"), &GameplayAbility::get_cooldown_tags);
	ClassDB::bind_method(D_METHOD("get_cooldown_time_remaining"), &GameplayAbility::get_cooldown_time_remaining);
	ClassDB::bind_method(D_METHOD("get_last_failure_reason"), &GameplayAbility::get_last_failure_reason);
	ClassDB::bind_method(D_METHOD("get_handle"), &GameplayAbility::get_handle);
	ClassDB::bind_method(D_METHOD("is_predicting"), &GameplayAbility::is_predicting);

	ClassDB::bind_method(D_METHOD("get_ability_level"), &GameplayAbility::get_ability_level);
	ClassDB::bind_method(D_METHOD("set_ability_level", "value"), &GameplayAbility::set_ability_level);

	ClassDB::bind_method(D_METHOD("get_net_execution_policy"), &GameplayAbility::get_net_execution_policy);
	ClassDB::bind_method(D_METHOD("set_net_execution_policy", "value"), &GameplayAbility::set_net_execution_policy);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "net_execution_policy", PROPERTY_HINT_ENUM, "Local Only,Local Predicted,Server Only"), "set_net_execution_policy", "get_net_execution_policy");

	ClassDB::bind_method(D_METHOD("get_cost_effect"), &GameplayAbility::get_cost_effect);
	ClassDB::bind_method(D_METHOD("set_cost_effect", "value"), &GameplayAbility::set_cost_effect);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "cost_effect", PROPERTY_HINT_RESOURCE_TYPE, "GameplayEffect"), "set_cost_effect", "get_cost_effect");

	ClassDB::bind_method(D_METHOD("get_trigger_event_tags"), &GameplayAbility::get_trigger_event_tags);
	ClassDB::bind_method(D_METHOD("set_trigger_event_tags", "value"), &GameplayAbility::set_trigger_event_tags);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "trigger_event_tags", PROPERTY_HINT_RESOURCE_TYPE, "GameplayTagContainer"), "set_trigger_event_tags", "get_trigger_event_tags");
	ClassDB::bind_method(D_METHOD("is_triggered_by", "event_tag"), &GameplayAbility::is_triggered_by);
	ClassDB::bind_method(D_METHOD("get_trigger_event_tag"), &GameplayAbility::get_trigger_event_tag);
	ClassDB::bind_method(D_METHOD("get_trigger_event_data"), &GameplayAbility::get_trigger_event_data);

	ClassDB::bind_method(D_METHOD("get_cooldown_effect"), &GameplayAbility::get_cooldown_effect);
	ClassDB::bind_method(D_METHOD("set_cooldown_effect", "value"), &GameplayAbility::set_cooldown_effect);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "cooldown_effect", PROPERTY_HINT_RESOURCE_TYPE, "GameplayEffect"), "set_cooldown_effect", "get_cooldown_effect");

	BIND_ENUM_CONSTANT(LOCAL_ONLY);
	BIND_ENUM_CONSTANT(LOCAL_PREDICTED);
	BIND_ENUM_CONSTANT(SERVER_ONLY);

	GDVIRTUAL_BIND(_on_give_ability);
	GDVIRTUAL_BIND(_can_activate_ability);
	GDVIRTUAL_BIND(_activate_ability);
	GDVIRTUAL_BIND(_end_ability, "was_canceled");
	GDVIRTUAL_BIND(_input_pressed);
	GDVIRTUAL_BIND(_input_released);
	GDVIRTUAL_BIND(_on_remove_ability);
	GDVIRTUAL_BIND(_should_respond_to_event, "event_tag", "payload");

	ADD_SIGNAL(MethodInfo("ability_ended", PropertyInfo(Variant::BOOL, "was_canceled")));
}
}
