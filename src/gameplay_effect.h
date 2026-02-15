#ifndef GAMEPLAY_EFFECT_H
#define GAMEPLAY_EFFECT_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/string_name.hpp>

using namespace godot;

namespace GFGD 
{
class GameplayTagContainer;

class GameplayEffect : public Resource
{
	GDCLASS(GameplayEffect, Resource)

public:
	enum DurationPolicy
	{
		INSTANT,
		INFINITE,
		HAS_DURATION
	};

private:
	DurationPolicy duration_policy;
	float period;

	Ref<GameplayTagContainer> granted_tags_added;
	Ref<GameplayTagContainer> granted_tags_removed;
	Ref<GameplayTagContainer> ongoing_tag_requirements_required;
	Ref<GameplayTagContainer> ongoing_tag_requirements_ignored;
	Ref<GameplayTagContainer> application_tag_requirements_required;
	Ref<GameplayTagContainer> application_tag_requirements_ignored;
	Ref<GameplayTagContainer> removal_tag_requirements_required;
	Ref<GameplayTagContainer> removal_tag_requirements_ignored;
	Ref<GameplayTagContainer> remove_gameplay_effects_with_tags;

public:
	GameplayEffect();
	~GameplayEffect();

	DurationPolicy get_duration_policy() const { return duration_policy; }
	void set_duration_policy(DurationPolicy value) { duration_policy = value; }

	float get_period() const { return period; }
	void set_period(float value) { period = value; }

	Ref<GameplayTagContainer> get_granted_tags_added() const { return granted_tags_added; }
	Ref<GameplayTagContainer> get_granted_tags_removed() const { return granted_tags_removed; }
	Ref<GameplayTagContainer> get_ongoing_tag_requirements_required() const { return ongoing_tag_requirements_required; }
	Ref<GameplayTagContainer> get_ongoing_tag_requirements_ignored() const { return ongoing_tag_requirements_ignored; }
	Ref<GameplayTagContainer> get_application_tag_requirements_required() const { return application_tag_requirements_required; }
	Ref<GameplayTagContainer> get_application_tag_requirements_ignored() const { return application_tag_requirements_ignored; }
	Ref<GameplayTagContainer> get_removal_tag_requirements_required() const { return removal_tag_requirements_required; }
	Ref<GameplayTagContainer> get_removal_tag_requirements_ignored() const { return removal_tag_requirements_ignored; }
	Ref<GameplayTagContainer> get_remove_gameplay_effects_with_tags() const { return remove_gameplay_effects_with_tags; }

protected:
	static void _bind_methods();
};

}

#endif