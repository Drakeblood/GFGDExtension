#include "ability_system_component.h"
#include <godot_cpp/core/class_db.hpp>

#include "gameplay_ability.h"
#include "gameplay_tag_count_container.h"
#include "gameplay_tag_container.h"
#include "gameplay_tag.h"

using namespace godot;

namespace GFGD
{
AbilitySystemComponent::AbilitySystemComponent()
{
	gameplay_tag_count_container.instantiate();
	blocked_ability_tags.instantiate();
}

AbilitySystemComponent::~AbilitySystemComponent()
{
	
}

void AbilitySystemComponent::_ready()
{
	for (int i = 0; i < startup_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = startup_abilities[i];
		if (ability.is_valid())
		{
			give_ability(ability);
		}
	}
}

void AbilitySystemComponent::give_ability(const Ref<GameplayAbility>& ability_template, const Variant& source_object)
{
	if (ability_template.is_null()) return;

	Ref<GameplayAbility> ability = ability_template->duplicate();
	activatable_abilities.append(ability);

	ability->setup_ability(this, source_object);
	ability->on_give_ability();
}

void AbilitySystemComponent::clear_ability(const Ref<GameplayAbility>& ability)
{
	if (ability.is_null()) return;

	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> current_ability = activatable_abilities[i];
		if (current_ability == ability)
		{
			if (current_ability->is_active())
			{
				current_ability->connect("ability_ended", Callable(this, "clear_ability").bind(ability));
				return;
			}

			activatable_abilities.remove_at(i);
			return;
		}
	}
}

bool AbilitySystemComponent::try_activate_ability(const StringName& ability_class_name)
{
	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = activatable_abilities[i];
		if (ability.is_valid() && ability->get_class() == ability_class_name)
		{
			if (!ability->can_activate_ability()) return false;

			ability->activate_ability();
			return true;
		}
	}
	return false;
}

void AbilitySystemComponent::cancel_abilities_with_tags(const Ref<GameplayTagContainer>& tag_container)
{
	if (tag_container.is_null() || tag_container->get_length() == 0) return;

	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = activatable_abilities[i];
		if (!ability->is_active()) continue;

		if (ability->get_ability_tags().is_valid() && ability->get_ability_tags()->has_any(tag_container))
		{
			ability->end_ability(true);
		}
	}
}

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
	if (gameplay_tag_count_container->update_tag_map(tag, count_delta))
	{
		on_tag_updated(tag, count_delta > 0);
	}
}

void AbilitySystemComponent::register_gameplay_tag_event(const Ref<GameplayTag>& tag, const Callable& tag_delegate)
{
	gameplay_tag_count_container->register_gameplay_tag_event(tag, tag_delegate);
}

void AbilitySystemComponent::update_blocked_ability_tags(const Ref<GameplayTag>& tag, int count_delta)
{
	blocked_ability_tags->update_tag_map(tag, count_delta);
}

void AbilitySystemComponent::ability_local_input_pressed(const StringName& action_name)
{
	for (int i = 0; i < activatable_abilities.size(); i++)
	{
		Ref<GameplayAbility> ability = activatable_abilities[i];
		if (ability.is_valid() && ability->get_input_action_name() == action_name)
		{
			ability->set_is_input_pressed(true);

			if (ability->is_active())
			{
				ability->input_pressed();
			}
			else
			{
				try_activate_ability(ability->get_class());
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

			if (ability->is_active())
			{
				ability->input_released();
			}
		}
	}
}

void AbilitySystemComponent::on_tag_updated(const Ref<GameplayTag>& tag, bool tag_exists)
{
	// Implement tag update logic if needed
}

void AbilitySystemComponent::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("_ready"), &AbilitySystemComponent::_ready);
	
	ClassDB::bind_method(D_METHOD("give_ability", "ability_template", "source_object"), &AbilitySystemComponent::give_ability);
	ClassDB::bind_method(D_METHOD("clear_ability", "ability"), &AbilitySystemComponent::clear_ability);
	ClassDB::bind_method(D_METHOD("try_activate_ability", "ability_class_name"), &AbilitySystemComponent::try_activate_ability);
	ClassDB::bind_method(D_METHOD("cancel_abilities_with_tags", "tag_container"), &AbilitySystemComponent::cancel_abilities_with_tags);
	
	ClassDB::bind_method(D_METHOD("get_owned_gameplay_tags"), &AbilitySystemComponent::get_owned_gameplay_tags);
	ClassDB::bind_method(D_METHOD("get_blocked_ability_tags"), &AbilitySystemComponent::get_blocked_ability_tags);
	
	ClassDB::bind_method(D_METHOD("update_tag_map", "tag", "count_delta"), &AbilitySystemComponent::update_tag_map);
	ClassDB::bind_method(D_METHOD("register_gameplay_tag_event", "tag", "tag_delegate"), &AbilitySystemComponent::register_gameplay_tag_event);
	ClassDB::bind_method(D_METHOD("update_blocked_ability_tags", "tag", "count_delta"), &AbilitySystemComponent::update_blocked_ability_tags);
	
	ClassDB::bind_method(D_METHOD("ability_local_input_pressed", "action_name"), &AbilitySystemComponent::ability_local_input_pressed);
	ClassDB::bind_method(D_METHOD("ability_local_input_released", "action_name"), &AbilitySystemComponent::ability_local_input_released);
}
}