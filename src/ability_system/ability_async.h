#ifndef ABILITY_ASYNC_H
#define ABILITY_ASYNC_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/dictionary.hpp>

using namespace godot;

namespace GFGD
{
class AbilitySystemComponent;
class GameplayEffect;

// Something to wait for on an AbilitySystemComponent from anywhere - a HUD, a
// quest, a door - rather than from inside an ability. Unreal's UAbilityAsync.
//
// An AbilityTask belongs to an ability and dies with it. An AbilityAsync
// belongs to the component it watches, the target: it is given one when it is
// made, the target keeps it alive, and it ends when the target is freed, when
// the optional owner node leaves the tree, when end_action() is called - or,
// with only_trigger_once, after the first time it triggers.
//
//   # A health bar: every change, for as long as the bar exists.
//   AbilityAsync.wait_attribute_change(asc, &"health", self).triggered.connect(_on_health)
//
//   # Once, awaited.
//   await AbilityAsync.wait_gameplay_tag_removed(asc, &"State.Stunned", self, true).triggered
//
// An ended async never triggers again, and releases whatever awaits it: the
// code after `await async.triggered` does not run once it has ended. That is
// the same promise an AbilityTask makes, and it is kept the same way - by
// dropping every connection.
//
// Subclass it in GDScript for one of your own: override _activate (hook into
// get_ability_system_component() there), call broadcast(result) when it
// happens, undo the hooks in _on_end, and start it with activate(target).
class AbilityAsync : public RefCounted
{
	GDCLASS(AbilityAsync, RefCounted)

private:
	ObjectID target_id;
	ObjectID owner_id;
	bool only_trigger_once;
	bool started;
	bool ended;
	bool cancelled;

public:
	AbilityAsync();
	~AbilityAsync();

	// Starts watching target. owner, when given, ends it on leaving the tree -
	// a widget's async dies with the widget. Returns itself, to chain.
	Ref<AbilityAsync> activate(AbilitySystemComponent* target, Node* owner = nullptr, bool trigger_once = false);

	// Reports that the thing waited for happened. Emits `triggered` at once when
	// something is listening, and at the end of the frame otherwise - an async
	// satisfied as it starts (a tag already there) would otherwise emit before
	// the caller had connected to it. With only_trigger_once, the async ends.
	void broadcast(const Variant& result = Variant());

	// Stops it for good: no further triggers, everything awaiting released.
	void end_action();

	bool is_active() const { return started && !ended; }
	bool has_ended() const { return ended; }
	bool get_only_trigger_once() const { return only_trigger_once; }

	AbilitySystemComponent* get_ability_system_component() const;
	Node* get_owner_node() const;

	// --- The built-in waits ---

	// The tag, or one under it, is owned: fires on the change from not having it
	// to having it, and at once if it is already there.
	static Ref<AbilityAsync> wait_gameplay_tag_added(AbilitySystemComponent* target, const StringName& tag, Node* owner = nullptr, bool trigger_once = false);

	// The opposite: fires when the last matching tag goes, and at once if none
	// is there.
	static Ref<AbilityAsync> wait_gameplay_tag_removed(AbilitySystemComponent* target, const StringName& tag, Node* owner = nullptr, bool trigger_once = false);

	// Any change to the attribute's current value. Result: {old_value, new_value}.
	static Ref<AbilityAsync> wait_attribute_change(AbilitySystemComponent* target, const StringName& attribute, Node* owner = nullptr, bool trigger_once = false);

	// A gameplay event with exactly this tag sent to the target. Result: the payload.
	static Ref<AbilityAsync> wait_gameplay_event(AbilitySystemComponent* target, const StringName& event_tag, Node* owner = nullptr, bool trigger_once = false);

	// An effect applied to the target - every one, or only those whose
	// effect_tags match effect_tag. Result: {effect, active_id}.
	static Ref<AbilityAsync> wait_gameplay_effect_applied(AbilitySystemComponent* target, const StringName& effect_tag = StringName(), Node* owner = nullptr, bool trigger_once = false);

	GDVIRTUAL0(_activate)
	GDVIRTUAL0(_on_end)

protected:
	static void _bind_methods();

	// For the built-in waits. on_activate runs once, from activate; on_end once,
	// when it ends for whatever reason, with the target still there if it is
	// being freed.
	virtual void on_activate();
	virtual void on_end();

	// Connects a signal of the target to one of this async's methods, and
	// disconnects it again - the part every built-in wait shares.
	void hook(const StringName& signal, const Callable& callback);
	void unhook(const StringName& signal, const Callable& callback);

private:
	void deliver(const Variant& result, const Ref<AbilityAsync>& keep);
	void stop();
	void release_listeners();
	void unregister();
	bool owner_gone() const;
};

// --- The built-in waits ---------------------------------------------------------
//
// Made by AbilityAsync's static wait_* methods; not meant to be built by hand.

class AbilityAsyncWaitTag : public AbilityAsync
{
	GDCLASS(AbilityAsyncWaitTag, AbilityAsync)

	StringName tag_name;
	bool wait_for_added = true;
	bool had_tag = false;

public:
	void setup(const StringName& name, bool for_added)
	{
		tag_name = name;
		wait_for_added = for_added;
	}

protected:
	static void _bind_methods() {}
	virtual void on_activate() override;
	virtual void on_end() override;

private:
	bool has_tag() const;
	void on_tag_changed(const StringName& changed_tag, int count);
};

class AbilityAsyncWaitAttribute : public AbilityAsync
{
	GDCLASS(AbilityAsyncWaitAttribute, AbilityAsync)

	StringName attribute_name;

public:
	void setup(const StringName& name) { attribute_name = name; }

protected:
	static void _bind_methods() {}
	virtual void on_activate() override;
	virtual void on_end() override;

private:
	void on_attribute_changed(const StringName& changed, double old_value, double new_value);
};

class AbilityAsyncWaitEvent : public AbilityAsync
{
	GDCLASS(AbilityAsyncWaitEvent, AbilityAsync)

	StringName event_tag;

public:
	void setup(const StringName& tag) { event_tag = tag; }

protected:
	static void _bind_methods() {}
	virtual void on_activate() override;
	virtual void on_end() override;

private:
	void on_event(const StringName& tag, const Dictionary& payload);
};

class AbilityAsyncWaitEffectApplied : public AbilityAsync
{
	GDCLASS(AbilityAsyncWaitEffectApplied, AbilityAsync)

	StringName effect_tag;

public:
	void setup(const StringName& tag) { effect_tag = tag; }

protected:
	static void _bind_methods() {}
	virtual void on_activate() override;
	virtual void on_end() override;

private:
	void on_effect_applied(const Ref<GameplayEffect>& effect, int64_t active_id);
};

}

#endif
