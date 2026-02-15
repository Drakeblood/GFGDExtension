#include "gameplay_tag_container.h"
#include <godot_cpp/core/class_db.hpp>

#include "gameplay_tag.h"

using namespace godot;

namespace GFGD
{
GameplayTagContainer::GameplayTagContainer()
{
	
}

void GameplayTagContainer::add_tag(const Ref<GameplayTag>& tag)
{
	if (!gameplay_tags.has(tag))
	{
		gameplay_tags.append(tag);
	}
}

void GameplayTagContainer::remove_tag(const Ref<GameplayTag>& tag)
{
	gameplay_tags.erase(tag);
}

bool GameplayTagContainer::has_tag(const Ref<GameplayTag>& tag_to_check) const
{
	if (has_tag_exact(tag_to_check)) { return true; }

	for (int i = 0; i < gameplay_tags.size(); i++)
	{
		Ref<GameplayTag> tag = gameplay_tags[i];
		if (tag.is_valid() && tag->matches_sub_tags(tag_to_check)) { return true; }
	}

	return false;
}

bool GameplayTagContainer::has_tag_exact(const Ref<GameplayTag>& tag_to_check) const
{
	return gameplay_tags.has(tag_to_check);
}

bool GameplayTagContainer::has_any(const Ref<GameplayTagContainer>& tag_container) const
{
	if (tag_container.is_null()) return false;

	for (int i = 0; i < tag_container->get_length(); i++)
	{
		Ref<GameplayTag> tag_to_check = tag_container->get_tag(i);
		for (int j = 0; j < gameplay_tags.size(); j++)
		{
			Ref<GameplayTag> tag = gameplay_tags[j];
			if (tag.is_valid() && tag->matches_tag(tag_to_check)) { return true; }
		}
	}
	return false;
}

bool GameplayTagContainer::has_all(const Ref<GameplayTagContainer>& tag_container) const
{
	if (tag_container.is_null()) return false;

	for (int i = 0; i < tag_container->get_length(); i++)
	{
		Ref<GameplayTag> tag_to_check = tag_container->get_tag(i);
		bool found = false;
		for (int j = 0; j < gameplay_tags.size(); j++)
		{
			Ref<GameplayTag> tag = gameplay_tags[j];
			if (tag.is_valid() && tag->matches_tag(tag_to_check))
			{
				found = true;
				break;
			}
		}
		if (!found) return false;
	}
	return true;
}

bool GameplayTagContainer::has_all_exact(const Ref<GameplayTagContainer>& tag_container) const
{
	if (tag_container.is_null()) return false;

	for (int i = 0; i < tag_container->get_length(); i++)
	{
		Ref<GameplayTag> tag_to_check = tag_container->get_tag(i);
		if (!gameplay_tags.has(tag_to_check)) return false;
	}
	return true;
}

Ref<GameplayTag> GameplayTagContainer::get_tag(int index) const
{
	if (index < 0 || index >= gameplay_tags.size()) return Ref<GameplayTag>();
	return gameplay_tags[index];
}

void GameplayTagContainer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("add_tag", "tag"), &GameplayTagContainer::add_tag);
	ClassDB::bind_method(D_METHOD("remove_tag", "tag"), &GameplayTagContainer::remove_tag);
	ClassDB::bind_method(D_METHOD("has_tag", "tag_to_check"), &GameplayTagContainer::has_tag);
	ClassDB::bind_method(D_METHOD("has_tag_exact", "tag_to_check"), &GameplayTagContainer::has_tag_exact);
	ClassDB::bind_method(D_METHOD("has_any", "tag_container"), &GameplayTagContainer::has_any);
	ClassDB::bind_method(D_METHOD("has_all", "tag_container"), &GameplayTagContainer::has_all);
	ClassDB::bind_method(D_METHOD("has_all_exact", "tag_container"), &GameplayTagContainer::has_all_exact);
	ClassDB::bind_method(D_METHOD("get_length"), &GameplayTagContainer::get_length);
	ClassDB::bind_method(D_METHOD("get_tag", "index"), &GameplayTagContainer::get_tag);
}
}