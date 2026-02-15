#include "gameplay_tag.h"
#include <godot_cpp/core/class_db.hpp>

#include "gameplay_tags_manager.h"

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
	if (this == tag_to_check.ptr()) { return true; }
	return matches_sub_tags(tag_to_check);
}

bool GameplayTag::matches_sub_tags(const Ref<GameplayTag>& tag_to_check) const
{
	Ref<GameplayTagsManager> manager = GameplayTagsManager::get_singleton();
	if (manager.is_null()) return false;

	Array separated_tag = manager->get_separated_tag(*this);
	Array separated_tag_to_check = manager->get_separated_tag(*tag_to_check);

	if (separated_tag_to_check.size() > separated_tag.size()) return false;

	for (int i = 0; i < separated_tag.size() && i < separated_tag_to_check.size(); i++)
	{
		Ref<GameplayTag> tag1 = separated_tag[i];
		Ref<GameplayTag> tag2 = separated_tag_to_check[i];
		if (tag1 != tag2) return false;
	}

	if (separated_tag_to_check.size() == 0 && separated_tag.size() > 0)
	{
		return separated_tag[0] == tag_to_check;
	}

	return true;
}

bool GameplayTag::operator==(const Ref<GameplayTag>& other) const
{
	if (this == other.ptr()) return true;
	if (other.is_null()) return false;
	return tag_name == other->tag_name;
}

bool GameplayTag::operator!=(const Ref<GameplayTag>& other) const
{
	return !(*this == other);
}

void GameplayTag::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_tag_name"), &GameplayTag::get_tag_name);
	ClassDB::bind_method(D_METHOD("matches_tag", "tag_to_check"), &GameplayTag::matches_tag);
	ClassDB::bind_method(D_METHOD("matches_sub_tags", "tag_to_check"), &GameplayTag::matches_sub_tags);
}
}