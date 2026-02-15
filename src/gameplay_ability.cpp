#include "gameplay_ability.h"
#include <godot_cpp/core/class_db.hpp>

#include "ability_system_component.h"
#include "gameplay_tag_container.h"

using namespace godot;

namespace GFGD
{
long GameplayAbility::id_counter = 0;

GameplayAbility::GameplayAbility()
{
	ability_id = -1;
	is_active = false;
	is_input_pressed = false;
	
	ability_tags.instantiate();
	cancel_abilities_with_tag.instantiate();
	block_abilities_with_tag.instantiate();
	activation_owned_tags.instantiate();
	activation_required_tags.instantiate();
	activation_blocked_tags.instantiate();
}

GameplayAbility::~GameplayAbility()
{
	
}

long GameplayAbility::get_id_counter()
{
	return id_counter;
}

void GameplayAbility::increment_id_counter()
{
	id_counter++;
}

void GameplayAbility::setup_ability(const Ref<AbilitySystemComponent>& ability_system_component, const Variant& source_object)
{
	ability_system_component = ability_system_component;
	source_object = source_object;

	ability_id = id_counter++;
	increment_id_counter();
}

void GameplayAbility::on_give_ability()
{
	
}

bool GameplayAbility::can_activate_ability()
{
	if (is_active) return false;
	if (ability_system_component.is_null()) return false;

	Ref<GameplayTagContainer> blocked_tags = ability_system_component->get_blocked_ability_tags();
	if (blocked_tags.is_valid() && ability_tags.is_valid() && ability_tags->has_any(blocked_tags))
	{
		return false;
	}

	if (activation_blocked_tags.is_valid() && activation_required_tags.is_valid())
	{
		Ref<GameplayTagContainer> owned_tags = ability_system_component->get_owned_gameplay_tags();
		if (owned_tags.is_valid())
		{
			if (owned_tags->has_any(activation_blocked_tags)) return false;
			if (!owned_tags->has_all(activation_required_tags)) return false;
		}
	}

	return true;
}

void GameplayAbility::activate_ability()
{
	if (ability_system_component.is_null()) return;
	is_active = true;

	if (activation_owned_tags.is_valid())
	{
		for (int i = 0; i < activation_owned_tags->get_length(); i++)
		{
			Ref<class GameplayTag> tag = activation_owned_tags->get_tag(i);
			if (tag.is_valid())
			{
				ability_system_component->update_tag_map(tag, 1);
			}
		}
	}

	if (block_abilities_with_tag.is_valid())
	{
		for (int i = 0; i < block_abilities_with_tag->get_length(); i++)
		{
			Ref<class GameplayTag> tag = block_abilities_with_tag->get_tag(i);
			if (tag.is_valid())
			{
				ability_system_component->update_blocked_ability_tags(tag, 1);
			}
		}
	}

	if (cancel_abilities_with_tag.is_valid())
	{
		ability_system_component->cancel_abilities_with_tags(cancel_abilities_with_tag);
	}
}

void GameplayAbility::end_ability(bool was_canceled)
{
	if (ability_system_component.is_null()) return;
	is_active = false;

	if (activation_owned_tags.is_valid())
	{
		for (int i = 0; i < activation_owned_tags->get_length(); i++)
		{
			Ref<class GameplayTag> tag = activation_owned_tags->get_tag(i);
			if (tag.is_valid())
			{
				ability_system_component->update_tag_map(tag, -1);
			}
		}
	}

	if (block_abilities_with_tag.is_valid())
	{
		for (int i = 0; i < block_abilities_with_tag->get_length(); i++)
		{
			Ref<class GameplayTag> tag = block_abilities_with_tag->get_tag(i);
			if (tag.is_valid())
			{
				ability_system_component->update_blocked_ability_tags(tag, -1);
			}
		}
	}

	emit_signal("ability_ended", was_canceled);
}

void GameplayAbility::input_pressed()
{
	
}

void GameplayAbility::input_released()
{
	
}

void GameplayAbility::_bind_methods()
{
	ClassDB::bind_static_method("GameplayAbility", D_METHOD("get_id_counter"), &GameplayAbility::get_id_counter);
	ClassDB::bind_static_method("GameplayAbility", D_METHOD("increment_id_counter"), &GameplayAbility::increment_id_counter);
	
	ClassDB::bind_method(D_METHOD("get_ability_id"), &GameplayAbility::get_ability_id);
	ClassDB::bind_method(D_METHOD("is_active"), &GameplayAbility::is_active);
	ClassDB::bind_method(D_METHOD("is_input_pressed"), &GameplayAbility::is_input_pressed);
	ClassDB::bind_method(D_METHOD("set_is_input_pressed", "value"), &GameplayAbility::set_is_input_pressed);
	ClassDB::bind_method(D_METHOD("get_source_object"), &GameplayAbility::get_source_object);
	
	ClassDB::bind_method(D_METHOD("get_ability_tags"), &GameplayAbility::get_ability_tags);
	ClassDB::bind_method(D_METHOD("get_cancel_abilities_with_tag"), &GameplayAbility::get_cancel_abilities_with_tag);
	ClassDB::bind_method(D_METHOD("get_block_abilities_with_tag"), &GameplayAbility::get_block_abilities_with_tag);
	ClassDB::bind_method(D_METHOD("get_activation_owned_tags"), &GameplayAbility::get_activation_owned_tags);
	ClassDB::bind_method(D_METHOD("get_activation_required_tags"), &GameplayAbility::get_activation_required_tags);
	ClassDB::bind_method(D_METHOD("get_activation_blocked_tags"), &GameplayAbility::get_activation_blocked_tags);
	
	ClassDB::bind_method(D_METHOD("get_input_action_name"), &GameplayAbility::get_input_action_name);
	
	ClassDB::bind_method(D_METHOD("setup_ability", "ability_system_component", "source_object"), &GameplayAbility::setup_ability);
	ClassDB::bind_method(D_METHOD("on_give_ability"), &GameplayAbility::on_give_ability);
	
	ClassDB::bind_method(D_METHOD("can_activate_ability"), &GameplayAbility::can_activate_ability);
	ClassDB::bind_method(D_METHOD("activate_ability"), &GameplayAbility::activate_ability);
	ClassDB::bind_method(D_METHOD("end_ability", "was_canceled"), &GameplayAbility::end_ability);
	
	ClassDB::bind_method(D_METHOD("input_pressed"), &GameplayAbility::input_pressed);
	ClassDB::bind_method(D_METHOD("input_released"), &GameplayAbility::input_released);
	
	ADD_SIGNAL(MethodInfo("ability_ended", PropertyInfo(Variant::BOOL, "was_canceled")));
}
}