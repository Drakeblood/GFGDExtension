#ifndef ABILITY_SYSTEM_COMPONENT_H
#define ABILITY_SYSTEM_COMPONENT_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/templates/vector.hpp>

#include "ability_system/attribute_set.h"
#include "ability_system/active_gameplay_effect.h"

using namespace godot;

namespace GFGD
{
class GameplayTag;
class GameplayAbility;
class GameplayTagCountContainer;
class GameplayTagContainer;
class GameplayEffect;

class AbilitySystemComponent : public Node
{
	GDCLASS(AbilitySystemComponent, Node)

private:
	Array startup_abilities;
	Array startup_effects;
	Ref<AttributeSet> attribute_set;

	Array activatable_abilities;
	GameplayTagCountContainer* gameplay_tag_count_container;
	GameplayTagCountContainer* blocked_ability_tags;

	Ref<AttributeSet> runtime_attribute_set;
	Vector<Ref<ActiveGameplayEffect>> active_effects;
	int64_t next_active_effect_id;

public:
	AbilitySystemComponent();
	~AbilitySystemComponent();

	virtual void _ready() override;
	virtual void _physics_process(double delta) override;

	// --- Abilities ---
	void give_ability(const Ref<GameplayAbility>& ability_template, const Variant& source_object = Variant());
	void clear_ability(const Ref<GameplayAbility>& ability);
	bool try_activate_ability(const StringName& ability_name);
	void cancel_abilities_with_tags(const Ref<GameplayTagContainer>& tag_container);
	Array get_activatable_abilities() const { return activatable_abilities; }

	void ability_local_input_pressed(const StringName& action_name);
	void ability_local_input_released(const StringName& action_name);

	// Internal notifications from GameplayAbility.
	void notify_ability_activated(const Ref<GameplayAbility>& ability);
	void notify_ability_ended(const Ref<GameplayAbility>& ability, bool was_canceled);

	// --- Tags ---
	Ref<GameplayTagContainer> get_owned_gameplay_tags() const;
	Ref<GameplayTagContainer> get_blocked_ability_tags() const;

	void update_tag_map(const Ref<GameplayTag>& tag, int count_delta);
	void register_gameplay_tag_event(const Ref<GameplayTag>& tag, const Callable& tag_delegate);
	void update_blocked_ability_tags(const Ref<GameplayTag>& tag, int count_delta);

	// --- Effects & attributes ---
	// Returns -1 when application was blocked, 0 for instant effects,
	// and the active effect id (> 0) for duration/infinite effects.
	int64_t apply_gameplay_effect_to_self(const Ref<GameplayEffect>& effect);
	bool remove_active_gameplay_effect(int64_t active_id);
	int remove_active_effects_with_tags(const Ref<GameplayTagContainer>& tag_container);
	Array get_active_effects() const;

	Ref<AttributeSet> get_attribute_set() const;
	double get_attribute_value(const StringName& attribute_name) const;
	double get_attribute_base_value(const StringName& attribute_name) const;
	void set_attribute_base_value(const StringName& attribute_name, double value);

	// --- Editor properties ---
	void set_startup_abilities(const Array& value) { startup_abilities = value; }
	Array get_startup_abilities() const { return startup_abilities; }
	void set_startup_effects(const Array& value) { startup_effects = value; }
	Array get_startup_effects() const { return startup_effects; }
	void set_attribute_set_template(const Ref<AttributeSet>& value) { attribute_set = value; }
	Ref<AttributeSet> get_attribute_set_template() const { return attribute_set; }

protected:
	static void _bind_methods();

private:
	void on_tag_updated(const Ref<GameplayTag>& tag, bool tag_exists);
	void _on_ability_ended_clear(bool was_canceled, const Ref<GameplayAbility>& ability);
	void _on_attribute_set_changed(const StringName& attribute_name, double old_value, double new_value);

	void apply_modifiers_to_base(const Ref<GameplayEffect>& effect);
	void grant_effect_tags(const Ref<GameplayEffect>& effect, int direction);
	void remove_active_effect_internal(int index);
	void recompute_all_attributes();
};

}

#endif
