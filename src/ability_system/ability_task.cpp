#include "ability_system/ability_task.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>

#include "ability_system/ability_system_component.h"
#include "ability_system/gameplay_ability.h"
#include "gameplay_tags/gameplay_tag.h"

using namespace godot;

namespace GFGD
{
AbilityTask::AbilityTask()
{
	started = false;
	finished = false;
	cancelled = false;
	ticking = false;
}

AbilityTask::~AbilityTask()
{
}

GameplayAbility* AbilityTask::get_ability() const
{
	return ability_id.is_valid() ? Object::cast_to<GameplayAbility>(ObjectDB::get_instance(ability_id)) : nullptr;
}

AbilitySystemComponent* AbilityTask::get_ability_system_component() const
{
	GameplayAbility* ability = get_ability();
	return ability != nullptr ? ability->get_ability_system_component() : nullptr;
}

void AbilityTask::set_parent(AbilityTask* parent)
{
	parent_id = parent != nullptr ? parent->get_instance_id() : ObjectID();
}

void AbilityTask::start(GameplayAbility* owning_ability)
{
	if (started) { return; }
	started = true;
	ability_id = owning_ability != nullptr ? owning_ability->get_instance_id() : ObjectID();

	on_activate();
	if (is_running())
	{
		GDVIRTUAL_CALL(_activate);
	}
}

void AbilityTask::tick(double delta)
{
	if (!is_running()) { return; }

	on_tick(delta);
	if (ticking && is_running())
	{
		GDVIRTUAL_CALL(_tick, delta);
	}
}

void AbilityTask::end(bool was_cancelled)
{
	on_end(was_cancelled);
	GDVIRTUAL_CALL(_on_end, was_cancelled);

	if (GameplayAbility* ability = get_ability())
	{
		ability->unregister_task(this);
	}
}

void AbilityTask::finish(const Variant& with_result)
{
	if (finished || cancelled) { return; }

	// Kept alive through the end of this call: the ability's list may hold the
	// last reference, and unregistering drops it.
	Ref<AbilityTask> keep(this);

	finished = true;
	result = with_result;
	end(false);

	if (AbilityTask* parent = parent_id.is_valid() ? Object::cast_to<AbilityTask>(ObjectDB::get_instance(parent_id)) : nullptr)
	{
		parent->on_child_finished(this);
	}

	if (!get_signal_connection_list("completed").is_empty())
	{
		emit_completed();
	}
	else
	{
		// Nothing is waiting yet - most likely the task finished while it was
		// being created, before the `await` on it began. The end of the frame is
		// after that await.
		callable_mp(this, &AbilityTask::emit_completed).call_deferred();
	}
}

void AbilityTask::emit_completed()
{
	if (cancelled) { return; }
	emit_signal("completed", result);
}

void AbilityTask::release_waiters()
{
	// Dropping every connection is what releases a suspended coroutine: the
	// connection held the only reference to its state, so it is freed, and the
	// code after its `await` never runs.
	const TypedArray<Dictionary> connections = get_signal_connection_list("completed");
	for (int i = 0; i < connections.size(); i++)
	{
		const Dictionary connection = connections[i];
		disconnect("completed", connection["callable"]);
	}
}

void AbilityTask::cancel()
{
	if (cancelled) { return; }

	Ref<AbilityTask> keep(this);

	const bool was_running = is_running();
	cancelled = true;
	release_waiters();

	if (was_running)
	{
		end(true);
	}
}

void AbilityTask::on_activate()
{
}

void AbilityTask::on_tick(double delta)
{
}

void AbilityTask::on_end(bool was_cancelled)
{
}

void AbilityTask::on_child_finished(AbilityTask* child)
{
}

void AbilityTask::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("finish", "result"), &AbilityTask::finish, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("cancel"), &AbilityTask::cancel);
	ClassDB::bind_method(D_METHOD("is_started"), &AbilityTask::is_started);
	ClassDB::bind_method(D_METHOD("is_finished"), &AbilityTask::is_finished);
	ClassDB::bind_method(D_METHOD("is_cancelled"), &AbilityTask::is_cancelled);
	ClassDB::bind_method(D_METHOD("is_running"), &AbilityTask::is_running);
	ClassDB::bind_method(D_METHOD("get_result"), &AbilityTask::get_result);
	ClassDB::bind_method(D_METHOD("get_ticking"), &AbilityTask::get_ticking);
	ClassDB::bind_method(D_METHOD("set_ticking", "value"), &AbilityTask::set_ticking);
	ClassDB::bind_method(D_METHOD("get_ability"), &AbilityTask::get_ability);
	ClassDB::bind_method(D_METHOD("get_ability_system_component"), &AbilityTask::get_ability_system_component);

	GDVIRTUAL_BIND(_activate);
	GDVIRTUAL_BIND(_tick, "delta");
	GDVIRTUAL_BIND(_on_end, "was_cancelled");

	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::NIL, "result", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
}

// --- WaitDelay ------------------------------------------------------------------

void AbilityTaskWaitDelay::on_activate()
{
	// On the simulation clock - the component's physics tick - rather than a
	// SceneTreeTimer, so it pauses with the game and counts the same ticks the
	// effects count.
	if (remaining <= 0.0)
	{
		finish();
	}
}

void AbilityTaskWaitDelay::on_tick(double delta)
{
	remaining -= delta;
	if (remaining <= 0.0)
	{
		finish();
	}
}

// --- WaitInput --------------------------------------------------------------------

void AbilityTaskWaitInput::on_activate()
{
	// Already in the state being waited for: a release waited for while the
	// button is up has happened.
	GameplayAbility* ability = get_ability();
	if (ability != nullptr && ability->get_is_input_pressed() == wait_for_press)
	{
		finish(0.0);
	}
}

void AbilityTaskWaitInput::on_tick(double delta)
{
	waited += delta;
}

void AbilityTaskWaitInput::notify_input(bool pressed)
{
	if (pressed == wait_for_press)
	{
		// How long it was held, or how long until it was pressed - what a charge
		// attack wants to know.
		finish(waited);
	}
}

// --- WaitTag ----------------------------------------------------------------------

void AbilityTaskWaitTag::on_activate()
{
	AbilitySystemComponent* asc = get_ability_system_component();
	if (asc == nullptr) { return; }

	asc->connect("owned_tag_changed", callable_mp(this, &AbilityTaskWaitTag::on_tag_changed));
	check();
}

void AbilityTaskWaitTag::on_end(bool was_cancelled)
{
	AbilitySystemComponent* asc = get_ability_system_component();
	const Callable callback = callable_mp(this, &AbilityTaskWaitTag::on_tag_changed);
	if (asc != nullptr && asc->is_connected("owned_tag_changed", callback))
	{
		asc->disconnect("owned_tag_changed", callback);
	}
}

void AbilityTaskWaitTag::on_tag_changed(const StringName& changed_tag, int count)
{
	check();
}

void AbilityTaskWaitTag::check()
{
	AbilitySystemComponent* asc = get_ability_system_component();
	if (asc == nullptr || !is_running()) { return; }

	Ref<GameplayTag> tag;
	tag.instantiate();
	tag->set_tag_name(tag_name);

	// Matched like every other tag query: waiting for "Status" is satisfied by
	// "Status.Stunned".
	if (asc->has_matching_gameplay_tag(tag) == wait_for_added)
	{
		finish(tag_name);
	}
}

// --- WaitAttribute ------------------------------------------------------------------

void AbilityTaskWaitAttribute::on_activate()
{
	AbilitySystemComponent* asc = get_ability_system_component();
	if (asc != nullptr)
	{
		asc->connect("attribute_changed", callable_mp(this, &AbilityTaskWaitAttribute::on_attribute_changed));
	}
}

void AbilityTaskWaitAttribute::on_end(bool was_cancelled)
{
	AbilitySystemComponent* asc = get_ability_system_component();
	const Callable callback = callable_mp(this, &AbilityTaskWaitAttribute::on_attribute_changed);
	if (asc != nullptr && asc->is_connected("attribute_changed", callback))
	{
		asc->disconnect("attribute_changed", callback);
	}
}

void AbilityTaskWaitAttribute::on_attribute_changed(const StringName& changed, double old_value, double new_value)
{
	if (changed != attribute_name) { return; }

	Dictionary change;
	change["old_value"] = old_value;
	change["new_value"] = new_value;
	finish(change);
}

// --- WaitEvent ----------------------------------------------------------------------

void AbilityTaskWaitEvent::on_activate()
{
	AbilitySystemComponent* asc = get_ability_system_component();
	if (asc != nullptr)
	{
		asc->connect("gameplay_event", callable_mp(this, &AbilityTaskWaitEvent::on_event));
	}
}

void AbilityTaskWaitEvent::on_end(bool was_cancelled)
{
	AbilitySystemComponent* asc = get_ability_system_component();
	const Callable callback = callable_mp(this, &AbilityTaskWaitEvent::on_event);
	if (asc != nullptr && asc->is_connected("gameplay_event", callback))
	{
		asc->disconnect("gameplay_event", callback);
	}
}

void AbilityTaskWaitEvent::on_event(const StringName& tag, const Dictionary& payload)
{
	if (tag == event_tag)
	{
		finish(payload);
	}
}

// --- WaitGroup (wait_any / wait_all) ---------------------------------------------------

void AbilityTaskWaitGroup::setup(const Array& tasks, bool for_all)
{
	children = tasks.duplicate();
	wait_for_all = for_all;
	for (int i = 0; i < children.size(); i++)
	{
		Ref<AbilityTask> child = children[i];
		if (child.is_valid())
		{
			child->set_parent(this);
		}
	}
}

void AbilityTaskWaitGroup::on_activate()
{
	// Some may have finished already, while they were being created.
	check_children();
}

void AbilityTaskWaitGroup::on_child_finished(AbilityTask* child)
{
	check_children();
}

void AbilityTaskWaitGroup::check_children()
{
	if (!is_running()) { return; }

	if (wait_for_all)
	{
		Array results;
		for (int i = 0; i < children.size(); i++)
		{
			Ref<AbilityTask> child = children[i];
			if (child.is_null()) { results.push_back(Variant()); continue; }
			if (!child->is_finished()) { return; }
			results.push_back(child->get_result());
		}
		finish(results);
		return;
	}

	for (int i = 0; i < children.size(); i++)
	{
		Ref<AbilityTask> child = children[i];
		if (child.is_valid() && child->is_finished())
		{
			// The index of the first to finish; its own result is on it.
			finish(i);
			return;
		}
	}
}

void AbilityTaskWaitGroup::on_end(bool was_cancelled)
{
	// The losers of a race, or everything when the group itself is cancelled.
	for (int i = 0; i < children.size(); i++)
	{
		Ref<AbilityTask> child = children[i];
		if (child.is_valid() && !child->is_finished())
		{
			child->cancel();
		}
	}
}

// --- SyncData ----------------------------------------------------------------------------

void AbilityTaskSyncData::on_activate()
{
	GameplayAbility* ability = get_ability();
	AbilitySystemComponent* asc = get_ability_system_component();
	if (ability == nullptr || asc == nullptr)
	{
		finish(local_data);
		return;
	}

	if (asc->is_replicated_client())
	{
		// The predicting client's value is the one both sides use. Sent, and used
		// here at once - the client does not wait for itself.
		asc->send_ability_target_data(Ref<GameplayAbility>(ability), sequence, local_data);
		finish(local_data);
		return;
	}

	if (!asc->expects_client_target_data(Ref<GameplayAbility>(ability)))
	{
		// Nobody else to hear from: offline, the host's own pawn, an activation
		// the server started itself.
		finish(local_data);
		return;
	}

	// The server's copy of an activation a client asked for: its own value is
	// discarded, and it waits for the client's - which may already be here.
	Variant received;
	if (ability->take_received_target_data(sequence, received))
	{
		finish(received);
	}
}

}
