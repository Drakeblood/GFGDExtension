#ifndef GAMEPLAY_ABILITY_H
#define GAMEPLAY_ABILITY_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

#include "gameplay_tags/gameplay_tag_container.h"

using namespace godot;

namespace GFGD
{
class AbilitySystemComponent;
class GameplayEffect;

class GameplayAbility : public Resource
{
	GDCLASS(GameplayAbility, Resource)

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

	void setup_ability(AbilitySystemComponent* ability_system_component, const Variant& source_object = Variant());
	void on_give_ability();

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

protected:
	static void _bind_methods();

private:
	void apply_activation_tags(int direction);
};

}

#endif
