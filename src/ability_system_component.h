#ifndef ABILITY_SYSTEM_COMPONENT_H
#define ABILITY_SYSTEM_COMPONENT_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/array.hpp>

using namespace godot;

namespace GFGD 
{
class GameplayAbility;
class GameplayTagCountContainer;
class GameplayTagContainer;

class AbilitySystemComponent : public Node
{
	GDCLASS(AbilitySystemComponent, Node)

private:
	Array startup_abilities;
	Array activatable_abilities;
	Ref<GameplayTagCountContainer> gameplay_tag_count_container;
	Ref<GameplayTagCountContainer> blocked_ability_tags;

public:
	AbilitySystemComponent();
	~AbilitySystemComponent();

	virtual void _ready() override;

	void give_ability(const Ref<GameplayAbility>& ability_template, const Variant& source_object = Variant());
	void clear_ability(const Ref<GameplayAbility>& ability);
	bool try_activate_ability(const StringName& ability_class_name);
	void cancel_abilities_with_tags(const Ref<GameplayTagContainer>& tag_container);

	Ref<GameplayTagContainer> get_owned_gameplay_tags() const;
	Ref<GameplayTagContainer> get_blocked_ability_tags() const;

	void update_tag_map(const Ref<class GameplayTag>& tag, int count_delta);
	void register_gameplay_tag_event(const Ref<class GameplayTag>& tag, const Callable& tag_delegate);
	void update_blocked_ability_tags(const Ref<class GameplayTag>& tag, int count_delta);

	void ability_local_input_pressed(const StringName& action_name);
	void ability_local_input_released(const StringName& action_name);

protected:
	static void _bind_methods();

private:
	void on_tag_updated(const Ref<class GameplayTag>& tag, bool tag_exists);
};

}

#endif