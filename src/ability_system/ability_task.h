#ifndef ABILITY_TASK_H
#define ABILITY_TASK_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace godot;

namespace GFGD
{
class AbilitySystemComponent;
class GameplayAbility;

// Something an ability waits for, shaped for GDScript's await.
//
// Unreal needs AbilityTasks because C++ and Blueprints have no coroutines:
// every "wait for X" has to be an object with callbacks. GDScript has await, so
// the waiting itself is free; what a task adds is the two things await cannot
// do on its own.
//
// It dies with its ability. When the ability ends - finished, cancelled, or a
// prediction the server refused - every task it is running is cancelled, and a
// cancelled task never emits: the connections to `completed` are dropped, so a
// coroutine suspended on it is released without ever resuming. The code after
// `await wait_delay(1.0).completed` simply does not run for an ability that was
// cancelled in the meantime - no `if not get_is_active(): return` to forget.
//
// And some waits need the network. sync_target_data carries a value from the
// predicting client to the server's copy of the same activation.
//
//   var which: int = await wait_any([wait_input_release(), wait_delay(2.0)]).completed
//
// Subclass it in GDScript for a task of your own: override _activate, call
// finish(result) when done, set_ticking(true) for _tick(delta) every physics
// frame, and start it with GameplayAbility.run_task.
class AbilityTask : public RefCounted
{
	GDCLASS(AbilityTask, RefCounted)

private:
	ObjectID ability_id;
	ObjectID parent_id;
	bool started;
	bool finished;
	bool cancelled;
	bool ticking;
	Variant result;

public:
	AbilityTask();
	~AbilityTask();

	// Starts the task for an ability. Called by GameplayAbility.run_task.
	void start(GameplayAbility* owning_ability);

	// Ends the task with a result. Emits `completed` at once when something is
	// awaiting it, and at the end of the frame otherwise - a task that finishes
	// while it is being created (a tag that is already there) would otherwise
	// emit before the `await` it was created for had begun.
	void finish(const Variant& with_result = Variant());

	// Ends the task without emitting. Whatever awaits it is released and never
	// resumes.
	void cancel();

	bool is_started() const { return started; }
	bool is_finished() const { return finished; }
	bool is_cancelled() const { return cancelled; }
	bool is_running() const { return started && !finished && !cancelled; }
	Variant get_result() const { return result; }

	bool get_ticking() const { return ticking; }
	void set_ticking(bool value) { ticking = value; }

	GameplayAbility* get_ability() const;
	AbilitySystemComponent* get_ability_system_component() const;

	void tick(double delta);

	// Set by wait_any and wait_all on the tasks they combine.
	void set_parent(AbilityTask* parent);

	GDVIRTUAL0(_activate)
	GDVIRTUAL1(_tick, double)
	GDVIRTUAL1(_on_end, bool)

protected:
	static void _bind_methods();

	// For the built-in tasks. Called once, from start.
	virtual void on_activate();
	virtual void on_tick(double delta);

	// Called once, when the task finishes or is cancelled - where hooks into the
	// component are taken down.
	virtual void on_end(bool was_cancelled);

	// A combining task hears about the tasks it combines through this.
	virtual void on_child_finished(AbilityTask* child);

private:
	void emit_completed();
	void release_waiters();
	void end(bool was_cancelled);
};

// --- The built-in tasks ----------------------------------------------------------
//
// Made by GameplayAbility's wait_* methods; not meant to be built by hand.

class AbilityTaskWaitDelay : public AbilityTask
{
	GDCLASS(AbilityTaskWaitDelay, AbilityTask)

	double remaining = 0.0;

public:
	void setup(double seconds) { remaining = seconds; }

protected:
	static void _bind_methods() {}
	virtual void on_activate() override;
	virtual void on_tick(double delta) override;
};

class AbilityTaskWaitInput : public AbilityTask
{
	GDCLASS(AbilityTaskWaitInput, AbilityTask)

	bool wait_for_press = false;
	double waited = 0.0;

public:
	void setup(bool for_press) { wait_for_press = for_press; }
	void notify_input(bool pressed);

protected:
	static void _bind_methods() {}
	virtual void on_activate() override;
	virtual void on_tick(double delta) override;
};

class AbilityTaskWaitTag : public AbilityTask
{
	GDCLASS(AbilityTaskWaitTag, AbilityTask)

	StringName tag_name;
	bool wait_for_added = true;

public:
	void setup(const StringName& name, bool for_added)
	{
		tag_name = name;
		wait_for_added = for_added;
	}

protected:
	static void _bind_methods() {}
	virtual void on_activate() override;
	virtual void on_end(bool was_cancelled) override;

private:
	void on_tag_changed(const StringName& changed_tag, int count);
	void check();
};

class AbilityTaskWaitAttribute : public AbilityTask
{
	GDCLASS(AbilityTaskWaitAttribute, AbilityTask)

	StringName attribute_name;

public:
	void setup(const StringName& name) { attribute_name = name; }

protected:
	static void _bind_methods() {}
	virtual void on_activate() override;
	virtual void on_end(bool was_cancelled) override;

private:
	void on_attribute_changed(const StringName& changed, double old_value, double new_value);
};

class AbilityTaskWaitEvent : public AbilityTask
{
	GDCLASS(AbilityTaskWaitEvent, AbilityTask)

	StringName event_tag;

public:
	void setup(const StringName& tag) { event_tag = tag; }

protected:
	static void _bind_methods() {}
	virtual void on_activate() override;
	virtual void on_end(bool was_cancelled) override;

private:
	void on_event(const StringName& tag, const Dictionary& payload);
};

class AbilityTaskWaitGroup : public AbilityTask
{
	GDCLASS(AbilityTaskWaitGroup, AbilityTask)

	Array children;
	bool wait_for_all = false;

public:
	void setup(const Array& tasks, bool for_all);

protected:
	static void _bind_methods() {}
	virtual void on_activate() override;
	virtual void on_end(bool was_cancelled) override;
	virtual void on_child_finished(AbilityTask* child) override;

private:
	void check_children();
};

class AbilityTaskSyncData : public AbilityTask
{
	GDCLASS(AbilityTaskSyncData, AbilityTask)

	Dictionary local_data;
	int sequence = 0;

public:
	void setup(const Dictionary& data, int seq)
	{
		local_data = data;
		sequence = seq;
	}
	int get_sequence() const { return sequence; }

protected:
	static void _bind_methods() {}
	virtual void on_activate() override;
};

}

#endif
