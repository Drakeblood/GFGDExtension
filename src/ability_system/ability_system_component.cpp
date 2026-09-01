#include "ability_system/ability_system_component.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/engine.hpp>

#include "ability_system/gameplay_ability.h"
#include "gameplay_tags/gameplay_tag_count_container.h"
#include "gameplay_tags/gameplay_tag_container.h"
#include "gameplay_tags/gameplay_tag.h"
#include "ability_system/gameplay_effect.h"
#include "ability_system/active_gameplay_effect.h"
#include "ability_system/attribute_set.h"
#include "ability_system/attribute_modifier.h"

using namespace godot;

namespace GFGD
{
AbilitySystemComponent::AbilitySystemComponent()
{
	gameplay_tag_count_container = memnew(GameplayTagCountContainer);
	blocked_ability_tags = memnew(GameplayTagCountContainer);
	next_active_effect_id = 1;
}

AbilitySystemComponent::~AbilitySystemComponent()
{
	memdelete(gameplay_tag_count_container);
	memdelete(blocked_ability_tags);
}

void AbilitySystemComponent::_ready()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	if (attribute_set.is_valid())
	{
		runtime_attribute_set = attribute_set->duplicate(true);
		runtime_attribute_set->init_current_from_base();
		runtime_attribute_set->connect("attribute_changed", callable_mp(this, &AbilitySystemComponent::_on_attribute_set_changed));
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

	set_physics_process(true);
}

void AbilitySystemComponent::_physics_process(double delta)
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

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
				apply_modifiers_to_base(effect);
				attributes_dirty = true;
			}
		}

		if (effect->get_duration_policy() == GameplayEffect::HAS_DURATION)
		{
			active->remaining_time -= delta;
			if (active->remaining_time <= 0.0)
			{
				remove_active_effect_internal(i);
				attributes_dirty = true;
			}
		}
	}

	if (attributes_dirty)
	{
		recompute_all_attributes();
	}
}

// --- Abilities ---

void AbilitySystemComponent::give_ability(const Ref<GameplayAbility>& ability_template, const Variant& source_object)
{
	if (ability_template.is_null()) { return; }

	Ref<GameplayAbility> ability = ability_template->duplicate();
	activatable_abilities.append(ability);

	ability->setup_ability(this, source_object);
	ability->on_give_ability();
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
			return;
		}
	}
}

void AbilitySystemComponent::_on_ability_ended_clear(bool was_canceled, const Ref<GameplayAbility>& ability)
{
	activatable_abilities.erase(ability);
}

bool AbilitySystemComponent::try_activate_ability(const StringName& ability_name)
{
	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = activatable_abilities[i];
		if (ability.is_valid() && ability->get_ability_name() == ability_name)
		{
			if (!ability->can_activate_ability()) { return false; }

			ability->activate_ability();
			return true;
		}
	}
	return false;
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

void AbilitySystemComponent::ability_local_input_pressed(const StringName& action_name)
{
	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = activatable_abilities[i];
		if (ability.is_valid() && ability->get_input_action_name() == action_name)
		{
			ability->set_is_input_pressed(true);

			if (ability->get_is_active())
			{
				ability->input_pressed();
			}
			else
			{
				try_activate_ability(ability->get_ability_name());
			}
		}
	}
}

void AbilitySystemComponent::ability_local_input_released(const StringName& action_name)
{
	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = activatable_abilities[i];
		if (ability.is_valid() && ability->get_input_action_name() == action_name)
		{
			ability->set_is_input_pressed(false);

			if (ability->get_is_active())
			{
				ability->input_released();
			}
		}
	}
}

void AbilitySystemComponent::notify_ability_activated(const Ref<GameplayAbility>& ability)
{
	emit_signal("ability_activated", ability);
}

void AbilitySystemComponent::notify_ability_ended(const Ref<GameplayAbility>& ability, bool was_canceled)
{
	emit_signal("ability_ended", ability, was_canceled);
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
	emit_signal("owned_tag_changed", tag->get_tag_name(), gameplay_tag_count_container->get_tag_count(tag->get_tag_name()));
}

void AbilitySystemComponent::register_gameplay_tag_event(const Ref<GameplayTag>& tag, const Callable& tag_delegate)
{
	gameplay_tag_count_container->register_gameplay_tag_event(tag, tag_delegate);
}

void AbilitySystemComponent::update_blocked_ability_tags(const Ref<GameplayTag>& tag, int count_delta)
{
	blocked_ability_tags->update_tag_count(tag, count_delta);
}

void AbilitySystemComponent::on_tag_updated(const Ref<GameplayTag>& tag, bool tag_exists)
{

}

// --- Effects & attributes ---

int64_t AbilitySystemComponent::apply_gameplay_effect_to_self(const Ref<GameplayEffect>& effect)
{
	if (effect.is_null()) { return -1; }

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
		apply_modifiers_to_base(effect);
		recompute_all_attributes();
		emit_signal("gameplay_effect_applied", effect, 0);
		return 0;
	}

	Ref<ActiveGameplayEffect> active;
	active.instantiate();
	active->effect = effect;
	active->active_id = next_active_effect_id++;
	active->remaining_time = (effect->get_duration_policy() == GameplayEffect::HAS_DURATION) ? effect->get_duration() : 0.0;
	active_effects.push_back(active);

	grant_effect_tags(effect, 1);
	recompute_all_attributes();

	emit_signal("gameplay_effect_applied", effect, active->active_id);
	return active->active_id;
}

bool AbilitySystemComponent::remove_active_gameplay_effect(int64_t active_id)
{
	for (int i = 0; i < active_effects.size(); i++)
	{
		if (active_effects[i]->active_id == active_id)
		{
			remove_active_effect_internal(i);
			recompute_all_attributes();
			return true;
		}
	}
	return false;
}

int AbilitySystemComponent::remove_active_effects_with_tags(const Ref<GameplayTagContainer>& tag_container)
{
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
}

void AbilitySystemComponent::apply_modifiers_to_base(const Ref<GameplayEffect>& effect)
{
	Ref<AttributeSet> set = get_attribute_set();
	if (set.is_null()) { return; }

	TypedArray<AttributeModifier> modifiers = effect->get_modifiers();
	for (int i = 0; i < modifiers.size(); i++)
	{
		Ref<AttributeModifier> modifier = modifiers[i];
		if (modifier.is_null() || modifier->get_attribute() == StringName()) { continue; }

		double base_value = set->get_base_value(modifier->get_attribute());
		set->set_base_value(modifier->get_attribute(), modifier->apply(base_value));
	}
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

void AbilitySystemComponent::remove_active_effect_internal(int index)
{
	Ref<ActiveGameplayEffect> active = active_effects[index];
	active_effects.remove_at(index);

	if (active->effect.is_valid())
	{
		grant_effect_tags(active->effect, -1);
	}
	emit_signal("gameplay_effect_removed", active->effect, active->active_id);
}

void AbilitySystemComponent::recompute_all_attributes()
{
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
			Ref<GameplayEffect> effect = active_effects[i]->effect;
			if (effect.is_null()) { continue; }
			// Periodic effects mutate base values on tick instead of aggregating.
			if (effect->get_period() > 0.0f) { continue; }

			TypedArray<AttributeModifier> modifiers = effect->get_modifiers();
			for (int m = 0; m < modifiers.size(); m++)
			{
				Ref<AttributeModifier> modifier = modifiers[m];
				if (modifier.is_null() || modifier->get_attribute() != attribute_name) { continue; }

				switch (modifier->get_operation())
				{
					case AttributeModifier::ADD:
						additive += modifier->get_magnitude();
						break;
					case AttributeModifier::MULTIPLY:
						multiplier *= modifier->get_magnitude();
						break;
					case AttributeModifier::OVERRIDE:
						has_override = true;
						override_value = modifier->get_magnitude();
						break;
				}
			}
		}

		double current_value = has_override ? override_value : (base_value + additive) * multiplier;
		set->set_current_value(attribute_name, current_value);
	}
}

void AbilitySystemComponent::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("give_ability", "ability_template", "source_object"), &AbilitySystemComponent::give_ability, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("clear_ability", "ability"), &AbilitySystemComponent::clear_ability);
	ClassDB::bind_method(D_METHOD("try_activate_ability", "ability_name"), &AbilitySystemComponent::try_activate_ability);
	ClassDB::bind_method(D_METHOD("cancel_abilities_with_tags", "tag_container"), &AbilitySystemComponent::cancel_abilities_with_tags);
	ClassDB::bind_method(D_METHOD("get_activatable_abilities"), &AbilitySystemComponent::get_activatable_abilities);

	ClassDB::bind_method(D_METHOD("get_owned_gameplay_tags"), &AbilitySystemComponent::get_owned_gameplay_tags);
	ClassDB::bind_method(D_METHOD("get_blocked_ability_tags"), &AbilitySystemComponent::get_blocked_ability_tags);

	ClassDB::bind_method(D_METHOD("update_tag_map", "tag", "count_delta"), &AbilitySystemComponent::update_tag_map);
	ClassDB::bind_method(D_METHOD("register_gameplay_tag_event", "tag", "tag_delegate"), &AbilitySystemComponent::register_gameplay_tag_event);
	ClassDB::bind_method(D_METHOD("update_blocked_ability_tags", "tag", "count_delta"), &AbilitySystemComponent::update_blocked_ability_tags);

	ClassDB::bind_method(D_METHOD("ability_local_input_pressed", "action_name"), &AbilitySystemComponent::ability_local_input_pressed);
	ClassDB::bind_method(D_METHOD("ability_local_input_released", "action_name"), &AbilitySystemComponent::ability_local_input_released);

	ClassDB::bind_method(D_METHOD("apply_gameplay_effect_to_self", "effect"), &AbilitySystemComponent::apply_gameplay_effect_to_self);
	ClassDB::bind_method(D_METHOD("remove_active_gameplay_effect", "active_id"), &AbilitySystemComponent::remove_active_gameplay_effect);
	ClassDB::bind_method(D_METHOD("remove_active_effects_with_tags", "tag_container"), &AbilitySystemComponent::remove_active_effects_with_tags);
	ClassDB::bind_method(D_METHOD("get_active_effects"), &AbilitySystemComponent::get_active_effects);

	ClassDB::bind_method(D_METHOD("get_attribute_set"), &AbilitySystemComponent::get_attribute_set);
	ClassDB::bind_method(D_METHOD("get_attribute_value", "attribute_name"), &AbilitySystemComponent::get_attribute_value);
	ClassDB::bind_method(D_METHOD("get_attribute_base_value", "attribute_name"), &AbilitySystemComponent::get_attribute_base_value);
	ClassDB::bind_method(D_METHOD("set_attribute_base_value", "attribute_name", "value"), &AbilitySystemComponent::set_attribute_base_value);

	ClassDB::bind_method(D_METHOD("set_startup_abilities", "value"), &AbilitySystemComponent::set_startup_abilities);
	ClassDB::bind_method(D_METHOD("get_startup_abilities"), &AbilitySystemComponent::get_startup_abilities);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "startup_abilities", PROPERTY_HINT_ARRAY_TYPE, vformat("%d/%d:GameplayAbility", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_startup_abilities", "get_startup_abilities");

	ClassDB::bind_method(D_METHOD("set_startup_effects", "value"), &AbilitySystemComponent::set_startup_effects);
	ClassDB::bind_method(D_METHOD("get_startup_effects"), &AbilitySystemComponent::get_startup_effects);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "startup_effects", PROPERTY_HINT_ARRAY_TYPE, vformat("%d/%d:GameplayEffect", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_startup_effects", "get_startup_effects");

	ClassDB::bind_method(D_METHOD("set_attribute_set_template", "value"), &AbilitySystemComponent::set_attribute_set_template);
	ClassDB::bind_method(D_METHOD("get_attribute_set_template"), &AbilitySystemComponent::get_attribute_set_template);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "attribute_set", PROPERTY_HINT_RESOURCE_TYPE, "AttributeSet"), "set_attribute_set_template", "get_attribute_set_template");

	ADD_SIGNAL(MethodInfo("ability_activated", PropertyInfo(Variant::OBJECT, "ability", PROPERTY_HINT_RESOURCE_TYPE, "GameplayAbility")));
	ADD_SIGNAL(MethodInfo("ability_ended", PropertyInfo(Variant::OBJECT, "ability", PROPERTY_HINT_RESOURCE_TYPE, "GameplayAbility"), PropertyInfo(Variant::BOOL, "was_canceled")));
	ADD_SIGNAL(MethodInfo("owned_tag_changed", PropertyInfo(Variant::STRING_NAME, "tag_name"), PropertyInfo(Variant::INT, "new_count")));
	ADD_SIGNAL(MethodInfo("gameplay_effect_applied", PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "GameplayEffect"), PropertyInfo(Variant::INT, "active_id")));
	ADD_SIGNAL(MethodInfo("gameplay_effect_removed", PropertyInfo(Variant::OBJECT, "effect", PROPERTY_HINT_RESOURCE_TYPE, "GameplayEffect"), PropertyInfo(Variant::INT, "active_id")));
	ADD_SIGNAL(MethodInfo("attribute_changed", PropertyInfo(Variant::STRING_NAME, "attribute_name"), PropertyInfo(Variant::FLOAT, "old_value"), PropertyInfo(Variant::FLOAT, "new_value")));
}
}
