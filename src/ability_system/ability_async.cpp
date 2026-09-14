#include "ability_system/ability_async.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "ability_system/ability_system_component.h"
#include "ability_system/gameplay_effect.h"
#include "gameplay_tags/gameplay_tag.h"
#include "gameplay_tags/gameplay_tag_container.h"

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
}

AbilityAsync::AbilityAsync()
{
	only_trigger_once = false;
	started = false;
	ended = false;
	cancelled = false;
}

AbilityAsync::~AbilityAsync()
{
}

AbilitySystemComponent* AbilityAsync::get_ability_system_component() const
{
	return target_id.is_valid() ? Object::cast_to<AbilitySystemComponent>(ObjectDB::get_instance(target_id)) : nullptr;
}

Node* AbilityAsync::get_owner_node() const
{
	return owner_id.is_valid() ? Object::cast_to<Node>(ObjectDB::get_instance(owner_id)) : nullptr;
}

bool AbilityAsync::owner_gone() const
{
	return owner_id.is_valid() && ObjectDB::get_instance(owner_id) == nullptr;
}

Ref<AbilityAsync> AbilityAsync::activate(AbilitySystemComponent* target, Node* owner, bool trigger_once)
{
	Ref<AbilityAsync> self(this);
	if (started) { return self; }
	started = true;
	only_trigger_once = trigger_once;

	if (target == nullptr)
	{
		UtilityFunctions::push_error("GFGD: an AbilityAsync was started with no AbilitySystemComponent to watch; it will never trigger.");
		ended = true;
		cancelled = true;
		return self;
	}

	target_id = target->get_instance_id();

	// The target holds it: an async nobody kept a variable for - one connected
	// to and forgotten, as a HUD does - still has to live.
	target->register_async(this);

	if (owner != nullptr)
	{
		owner_id = owner->get_instance_id();
		owner->connect("tree_exiting", callable_mp(this, &AbilityAsync::end_action));
	}

	on_activate();
	if (is_active())
	{
		GDVIRTUAL_CALL(_activate);
	}
	return self;
}

void AbilityAsync::broadcast(const Variant& result)
{
	if (!is_active()) { return; }

	Ref<AbilityAsync> keep(this);

	// An owner freed without ever leaving the tree sent no tree_exiting.
	if (owner_gone())
	{
		end_action();
		return;
	}

	if (only_trigger_once)
	{
		// Ended at once, so nothing more is heard, but still registered with the
		// target until delivered - it may be the only thing keeping this alive.
		stop();
	}

	if (!get_signal_connection_list("triggered").is_empty())
	{
		deliver(result, keep);
	}
	else
	{
		// Nothing listens yet: the async triggered while it was being made, before
		// the caller got to connect or await. The end of the frame is after that.
		callable_mp(this, &AbilityAsync::deliver).call_deferred(result, keep);
	}
}

void AbilityAsync::deliver(const Variant& result, const Ref<AbilityAsync>& keep)
{
	if (cancelled) { return; }

	emit_signal("triggered", result);

	if (ended)
	{
		cancelled = true;
		release_listeners();
		unregister();
	}
}

void AbilityAsync::end_action()
{
	if (cancelled) { return; }

	Ref<AbilityAsync> keep(this);
	cancelled = true;
	release_listeners();
	if (!ended)
	{
		stop();
	}
	unregister();
}

void AbilityAsync::stop()
{
	if (ended) { return; }
	ended = true;

	on_end();
	GDVIRTUAL_CALL(_on_end);

	Node* owner = get_owner_node();
	const Callable callback = callable_mp(this, &AbilityAsync::end_action);
	if (owner != nullptr && owner->is_connected("tree_exiting", callback))
	{
		owner->disconnect("tree_exiting", callback);
	}
}

void AbilityAsync::release_listeners()
{
	// Dropping every connection is what releases a suspended coroutine: the
	// connection held the only reference to its state, so it is freed and the
	// code after its `await` never runs.
	const TypedArray<Dictionary> connections = get_signal_connection_list("triggered");
	for (int i = 0; i < connections.size(); i++)
	{
		const Dictionary connection = connections[i];
		disconnect("triggered", connection["callable"]);
	}
}

void AbilityAsync::unregister()
{
	if (AbilitySystemComponent* target = get_ability_system_component())
	{
		target->unregister_async(this);
	}
}

void AbilityAsync::hook(const StringName& signal, const Callable& callback)
{
	AbilitySystemComponent* target = get_ability_system_component();
	if (target != nullptr && !target->is_connected(signal, callback))
	{
		target->connect(signal, callback);
	}
}

void AbilityAsync::unhook(const StringName& signal, const Callable& callback)
{
	AbilitySystemComponent* target = get_ability_system_component();
	if (target != nullptr && target->is_connected(signal, callback))
	{
		target->disconnect(signal, callback);
	}
}

void AbilityAsync::on_activate()
{
}

void AbilityAsync::on_end()
{
}

Ref<AbilityAsync> AbilityAsync::wait_gameplay_tag_added(AbilitySystemComponent* target, const StringName& tag, Node* owner, bool trigger_once)
{
	Ref<AbilityAsyncWaitTag> async;
	async.instantiate();
	async->setup(tag, true);
	return async->activate(target, owner, trigger_once);
}

Ref<AbilityAsync> AbilityAsync::wait_gameplay_tag_removed(AbilitySystemComponent* target, const StringName& tag, Node* owner, bool trigger_once)
{
	Ref<AbilityAsyncWaitTag> async;
	async.instantiate();
	async->setup(tag, false);
	return async->activate(target, owner, trigger_once);
}

Ref<AbilityAsync> AbilityAsync::wait_attribute_change(AbilitySystemComponent* target, const StringName& attribute, Node* owner, bool trigger_once)
{
	Ref<AbilityAsyncWaitAttribute> async;
	async.instantiate();
	async->setup(attribute);
	return async->activate(target, owner, trigger_once);
}

Ref<AbilityAsync> AbilityAsync::wait_gameplay_event(AbilitySystemComponent* target, const StringName& event_tag, Node* owner, bool trigger_once)
{
	Ref<AbilityAsyncWaitEvent> async;
	async.instantiate();
	async->setup(event_tag);
	return async->activate(target, owner, trigger_once);
}

Ref<AbilityAsync> AbilityAsync::wait_gameplay_effect_applied(AbilitySystemComponent* target, const StringName& effect_tag, Node* owner, bool trigger_once)
{
	Ref<AbilityAsyncWaitEffectApplied> async;
	async.instantiate();
	async->setup(effect_tag);
	return async->activate(target, owner, trigger_once);
}

void AbilityAsync::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("activate", "target", "owner", "only_trigger_once"), &AbilityAsync::activate, DEFVAL(Variant()), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("broadcast", "result"), &AbilityAsync::broadcast, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("end_action"), &AbilityAsync::end_action);
	ClassDB::bind_method(D_METHOD("is_active"), &AbilityAsync::is_active);
	ClassDB::bind_method(D_METHOD("has_ended"), &AbilityAsync::has_ended);
	ClassDB::bind_method(D_METHOD("get_only_trigger_once"), &AbilityAsync::get_only_trigger_once);
	ClassDB::bind_method(D_METHOD("get_ability_system_component"), &AbilityAsync::get_ability_system_component);
	ClassDB::bind_method(D_METHOD("get_owner_node"), &AbilityAsync::get_owner_node);

	ClassDB::bind_static_method("AbilityAsync", D_METHOD("wait_gameplay_tag_added", "target", "tag", "owner", "only_trigger_once"), &AbilityAsync::wait_gameplay_tag_added, DEFVAL(Variant()), DEFVAL(false));
	ClassDB::bind_static_method("AbilityAsync", D_METHOD("wait_gameplay_tag_removed", "target", "tag", "owner", "only_trigger_once"), &AbilityAsync::wait_gameplay_tag_removed, DEFVAL(Variant()), DEFVAL(false));
	ClassDB::bind_static_method("AbilityAsync", D_METHOD("wait_attribute_change", "target", "attribute", "owner", "only_trigger_once"), &AbilityAsync::wait_attribute_change, DEFVAL(Variant()), DEFVAL(false));
	ClassDB::bind_static_method("AbilityAsync", D_METHOD("wait_gameplay_event", "target", "event_tag", "owner", "only_trigger_once"), &AbilityAsync::wait_gameplay_event, DEFVAL(Variant()), DEFVAL(false));
	ClassDB::bind_static_method("AbilityAsync", D_METHOD("wait_gameplay_effect_applied", "target", "effect_tag", "owner", "only_trigger_once"), &AbilityAsync::wait_gameplay_effect_applied, DEFVAL(StringName()), DEFVAL(Variant()), DEFVAL(false));

	GDVIRTUAL_BIND(_activate);
	GDVIRTUAL_BIND(_on_end);

	ADD_SIGNAL(MethodInfo("triggered", PropertyInfo(Variant::NIL, "result", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
}

// --- WaitTag ----------------------------------------------------------------------

bool AbilityAsyncWaitTag::has_tag() const
{
	AbilitySystemComponent* target = get_ability_system_component();
	// Matched like every other tag query: "Status" is had while "Status.Stunned" is.
	return target != nullptr && target->has_matching_gameplay_tag(make_tag(tag_name));
}

void AbilityAsyncWaitTag::on_activate()
{
	hook("owned_tag_changed", callable_mp(this, &AbilityAsyncWaitTag::on_tag_changed));

	had_tag = has_tag();

	// Already in the state waited for: that counts, as in Unreal.
	if (had_tag == wait_for_added)
	{
		broadcast(tag_name);
	}
}

void AbilityAsyncWaitTag::on_end()
{
	unhook("owned_tag_changed", callable_mp(this, &AbilityAsyncWaitTag::on_tag_changed));
}

void AbilityAsyncWaitTag::on_tag_changed(const StringName& changed_tag, int count)
{
	// Only the edge: a second source granting the same tag is not a new arrival.
	const bool now = has_tag();
	if (now == had_tag) { return; }
	had_tag = now;

	if (now == wait_for_added)
	{
		broadcast(tag_name);
	}
}

// --- WaitAttribute ----------------------------------------------------------------

void AbilityAsyncWaitAttribute::on_activate()
{
	hook("attribute_changed", callable_mp(this, &AbilityAsyncWaitAttribute::on_attribute_changed));
}

void AbilityAsyncWaitAttribute::on_end()
{
	unhook("attribute_changed", callable_mp(this, &AbilityAsyncWaitAttribute::on_attribute_changed));
}

void AbilityAsyncWaitAttribute::on_attribute_changed(const StringName& changed, double old_value, double new_value)
{
	if (changed != attribute_name) { return; }

	Dictionary change;
	change["old_value"] = old_value;
	change["new_value"] = new_value;
	broadcast(change);
}

// --- WaitEvent --------------------------------------------------------------------

void AbilityAsyncWaitEvent::on_activate()
{
	hook("gameplay_event", callable_mp(this, &AbilityAsyncWaitEvent::on_event));
}

void AbilityAsyncWaitEvent::on_end()
{
	unhook("gameplay_event", callable_mp(this, &AbilityAsyncWaitEvent::on_event));
}

void AbilityAsyncWaitEvent::on_event(const StringName& tag, const Dictionary& payload)
{
	if (tag == event_tag)
	{
		broadcast(payload);
	}
}

// --- WaitEffectApplied ------------------------------------------------------------

void AbilityAsyncWaitEffectApplied::on_activate()
{
	hook("gameplay_effect_applied", callable_mp(this, &AbilityAsyncWaitEffectApplied::on_effect_applied));
}

void AbilityAsyncWaitEffectApplied::on_end()
{
	unhook("gameplay_effect_applied", callable_mp(this, &AbilityAsyncWaitEffectApplied::on_effect_applied));
}

void AbilityAsyncWaitEffectApplied::on_effect_applied(const Ref<GameplayEffect>& effect, int64_t active_id)
{
	if (effect.is_null()) { return; }

	if (effect_tag != StringName())
	{
		const Ref<GameplayTagContainer> tags = effect->get_effect_tags();
		if (tags.is_null() || !tags->has_tag(make_tag(effect_tag))) { return; }
	}

	Dictionary applied;
	applied["effect"] = effect;
	applied["active_id"] = active_id;
	broadcast(applied);
}
}
