#include "gameplay_tags/gameplay_tag.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
GameplayTag::GameplayTag()
{

}

GameplayTag::GameplayTag(const StringName& tag_name)
{
	this->tag_name = tag_name;
}

bool GameplayTag::matches_tag(const Ref<GameplayTag>& tag_to_check) const
{
	if (tag_to_check.is_null()) { return false; }
	if (tag_name == tag_to_check->tag_name) { return true; }

	return String(tag_name).begins_with(String(tag_to_check->tag_name) + ".");
}

bool GameplayTag::matches_tag_exact(const Ref<GameplayTag>& tag_to_check) const
{
	if (tag_to_check.is_null()) { return false; }
	return tag_name == tag_to_check->tag_name;
}

void GameplayTag::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_tag_name"), &GameplayTag::get_tag_name);
	ClassDB::bind_method(D_METHOD("set_tag_name", "value"), &GameplayTag::set_tag_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "tag_name"), "set_tag_name", "get_tag_name");

	ClassDB::bind_method(D_METHOD("matches_tag", "tag_to_check"), &GameplayTag::matches_tag);
	ClassDB::bind_method(D_METHOD("matches_tag_exact", "tag_to_check"), &GameplayTag::matches_tag_exact);
}
}
