#ifndef GAMEPLAY_ABILITY_H
#define GAMEPLAY_ABILITY_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/string_name.hpp>

using namespace godot;

namespace GFGD 
{
class AbilitySystemComponent;
class GameplayTagContainer;

class GameplayAbility : public Resource
{
	GDCLASS(GameplayAbility, Resource)

private:
	Ref<AbilitySystemComponent> ability_system_component;
	long ability_id;
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

	static long get_id_counter();
	static void increment_id_counter();

	long get_ability_id() const { return ability_id; }
	bool is_active() const { return is_active; }
	bool is_input_pressed() const { return is_input_pressed; }
	void set_is_input_pressed(bool value) { is_input_pressed = value; }
	Variant get_source_object() const { return source_object; }

	Ref<GameplayTagContainer> get_ability_tags() const { return ability_tags; }
	Ref<GameplayTagContainer> get_cancel_abilities_with_tag() const { return cancel_abilities_with_tag; }
	Ref<GameplayTagContainer> get_block_abilities_with_tag() const { return block_abilities_with_tag; }
	Ref<GameplayTagContainer> get_activation_owned_tags() const { return activation_owned_tags; }
	Ref<GameplayTagContainer> get_activation_required_tags() const { return activation_required_tags; }
	Ref<GameplayTagContainer> get_activation_blocked_tags() const { return activation_blocked_tags; }

	StringName get_input_action_name() const { return input_action_name; }

	void setup_ability(const Ref<AbilitySystemComponent>& ability_system_component, const Variant& source_object = Variant());
	virtual void on_give_ability();

	virtual bool can_activate_ability();
	virtual void activate_ability();
	virtual void end_ability(bool was_canceled = false);

	virtual void input_pressed();
	virtual void input_released();

protected:
	static void _bind_methods();
};

}

#endif