#include "gameplay_effect.h"
#include <godot_cpp/core/class_db.hpp>

#include "gameplay_tag_container.h"

using namespace godot;

namespace GFGD
{
GameplayEffect::GameplayEffect()
{
	duration_policy = INSTANT;
	period = 0.0f;
	
	granted_tags_added.instantiate();
	granted_tags_removed.instantiate();
	ongoing_tag_requirements_required.instantiate();
	ongoing_tag_requirements_ignored.instantiate();
	application_tag_requirements_required.instantiate();
	application_tag_requirements_ignored.instantiate();
	removal_tag_requirements_required.instantiate();
	removal_tag_requirements_ignored.instantiate();
	remove_gameplay_effects_with_tags.instantiate();
}

GameplayEffect::~GameplayEffect()
{
	
}

void GameplayEffect::_bind_methods()
{
	BIND_ENUM_CONSTANT(INSTANT);
	BIND_ENUM_CONSTANT(INFINITE);
	BIND_ENUM_CONSTANT(HAS_DURATION);
	
	ClassDB::bind_method(D_METHOD("get_duration_policy"), &GameplayEffect::get_duration_policy);
	ClassDB::bind_method(D_METHOD("set_duration_policy", "value"), &GameplayEffect::set_duration_policy);
	
	ClassDB::bind_method(D_METHOD("get_period"), &GameplayEffect::get_period);
	ClassDB::bind_method(D_METHOD("set_period", "value"), &GameplayEffect::set_period);
	
	ClassDB::bind_method(D_METHOD("get_granted_tags_added"), &GameplayEffect::get_granted_tags_added);
	ClassDB::bind_method(D_METHOD("get_granted_tags_removed"), &GameplayEffect::get_granted_tags_removed);
	ClassDB::bind_method(D_METHOD("get_ongoing_tag_requirements_required"), &GameplayEffect::get_ongoing_tag_requirements_required);
	ClassDB::bind_method(D_METHOD("get_ongoing_tag_requirements_ignored"), &GameplayEffect::get_ongoing_tag_requirements_ignored);
	ClassDB::bind_method(D_METHOD("get_application_tag_requirements_required"), &GameplayEffect::get_application_tag_requirements_required);
	ClassDB::bind_method(D_METHOD("get_application_tag_requirements_ignored"), &GameplayEffect::get_application_tag_requirements_ignored);
	ClassDB::bind_method(D_METHOD("get_removal_tag_requirements_required"), &GameplayEffect::get_removal_tag_requirements_required);
	ClassDB::bind_method(D_METHOD("get_removal_tag_requirements_ignored"), &GameplayEffect::get_removal_tag_requirements_ignored);
	ClassDB::bind_method(D_METHOD("get_remove_gameplay_effects_with_tags"), &GameplayEffect::get_remove_gameplay_effects_with_tags);
}
}