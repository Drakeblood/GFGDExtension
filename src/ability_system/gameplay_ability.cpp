#include "ability_system/gameplay_ability.h"
#include <godot_cpp/core/class_db.hpp>

#include "ability_system/ability_system_component.h"
#include "gameplay_tags/gameplay_tag_container.h"
#include "gameplay_tags/gameplay_tag.h"
#include "ability_system/gameplay_effect.h"

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

bool GameplayAbility::can_activate_ability()
{
	if (get_is_active()) { return false; }
	if (!ability_system_component) { return false; }

	Ref<GameplayTagContainer> blocked_tags = ability_system_component->get_blocked_ability_tags();
	if (blocked_tags.is_valid() && blocked_tags->has_any(ability_tags)) { return false; }

	Ref<GameplayTagContainer> owned_tags = ability_system_component->get_owned_gameplay_tags();
	if (owned_tags.is_valid())
	{
		if (owned_tags->has_any(activation_blocked_tags)) { return false; }
		if (!owned_tags->has_all(activation_required_tags)) { return false; }
	}

	bool script_allows = true;
	if (GDVIRTUAL_CALL(_can_activate_ability, script_allows))
	{
		if (!script_allows) { return false; }
	}

	return true;
}

void GameplayAbility::activate_ability()
{
	if (!ability_system_component || is_active) { return; }
	is_active = true;

	apply_activation_tags(1);

	if (cancel_abilities_with_tag.is_valid() && cancel_abilities_with_tag->get_length() > 0)
	{
		ability_system_component->cancel_abilities_with_tags(cancel_abilities_with_tag);
	}

	ability_system_component->notify_ability_activated(Ref<GameplayAbility>(this));
	GDVIRTUAL_CALL(_activate_ability);
}

void GameplayAbility::end_ability(bool was_canceled)
{
	if (!ability_system_component || !is_active) { return; }
	is_active = false;

	GDVIRTUAL_CALL(_end_ability, was_canceled);

	apply_activation_tags(-1);

	emit_signal("ability_ended", was_canceled);
	ability_system_component->notify_ability_ended(Ref<GameplayAbility>(this), was_canceled);
}

void GameplayAbility::input_pressed()
{
	GDVIRTUAL_CALL(_input_pressed);
}

void GameplayAbility::input_released()
{
	GDVIRTUAL_CALL(_input_released);
}

int64_t GameplayAbility::apply_effect_to_owner(const Ref<GameplayEffect>& effect)
{
	if (!ability_system_component) { return -1; }
	return ability_system_component->apply_gameplay_effect_to_self(effect);
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

	GDVIRTUAL_BIND(_on_give_ability);
	GDVIRTUAL_BIND(_can_activate_ability);
	GDVIRTUAL_BIND(_activate_ability);
	GDVIRTUAL_BIND(_end_ability, "was_canceled");
	GDVIRTUAL_BIND(_input_pressed);
	GDVIRTUAL_BIND(_input_released);

	ADD_SIGNAL(MethodInfo("ability_ended", PropertyInfo(Variant::BOOL, "was_canceled")));
}
}
