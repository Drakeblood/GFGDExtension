#include "gameplay_tags/gameplay_tag_container.h"
#include <godot_cpp/core/class_db.hpp>

#include "gameplay_tags/gameplay_tag.h"
#include "gameplay_tags/gameplay_tags_manager.h"

using namespace godot;

namespace GFGD
{
GameplayTagContainer::GameplayTagContainer()
{

}

void GameplayTagContainer::add_tag(const Ref<GameplayTag>& tag)
{
	if (tag.is_null()) { return; }

	if (find_tag_index(tag->get_tag_name()) == -1)
	{
		gameplay_tags.append(tag);
	}
}

void GameplayTagContainer::add_tag_by_name(const StringName& tag_name)
{
	add_tag(GameplayTagsManager::get_singleton()->request_tag(tag_name));
}

void GameplayTagContainer::remove_tag(const Ref<GameplayTag>& tag)
{
	if (tag.is_null()) { return; }

	int index = find_tag_index(tag->get_tag_name());
	if (index != -1)
	{
		gameplay_tags.remove_at(index);
	}
}

bool GameplayTagContainer::has_tag(const Ref<GameplayTag>& tag_to_check) const
{
	if (tag_to_check.is_null()) { return false; }

	for (int i = 0; i < gameplay_tags.size(); i++)
	{
		const Ref<GameplayTag>& tag = gameplay_tags[i];
		if (tag.is_valid() && tag->matches_tag(tag_to_check)) { return true; }
	}
	return false;
}

bool GameplayTagContainer::has_tag_exact(const Ref<GameplayTag>& tag_to_check) const
{
	if (tag_to_check.is_null()) { return false; }
	return find_tag_index(tag_to_check->get_tag_name()) != -1;
}

bool GameplayTagContainer::has_any(const Ref<GameplayTagContainer>& tag_container) const
{
	if (tag_container.is_null()) { return false; }

	for (int i = 0; i < tag_container->get_length(); i++)
	{
		if (has_tag(tag_container->get_tag(i))) { return true; }
	}
	return false;
}

bool GameplayTagContainer::has_all(const Ref<GameplayTagContainer>& tag_container) const
{
	if (tag_container.is_null()) { return true; }

	for (int i = 0; i < tag_container->get_length(); i++)
	{
		if (!has_tag(tag_container->get_tag(i))) { return false; }
	}
	return true;
}

bool GameplayTagContainer::has_all_exact(const Ref<GameplayTagContainer>& tag_container) const
{
	if (tag_container.is_null()) { return true; }

	for (int i = 0; i < tag_container->get_length(); i++)
	{
		if (!has_tag_exact(tag_container->get_tag(i))) { return false; }
	}
	return true;
}

Ref<GameplayTag> GameplayTagContainer::get_tag(int index) const
{
	if (index < 0 || index >= gameplay_tags.size()) { return Ref<GameplayTag>(); }
	return gameplay_tags[index];
}

void GameplayTagContainer::set_tags(const PackedStringArray& tag_names)
{
	gameplay_tags.clear();
	for (int i = 0; i < tag_names.size(); i++)
	{
		String tag_name = tag_names[i].strip_edges();
		if (!tag_name.is_empty())
		{
			add_tag_by_name(tag_name);
		}
	}
}

PackedStringArray GameplayTagContainer::get_tags() const
{
	PackedStringArray tag_names;
	for (int i = 0; i < gameplay_tags.size(); i++)
	{
		const Ref<GameplayTag>& tag = gameplay_tags[i];
		if (tag.is_valid())
		{
			tag_names.push_back(tag->get_tag_name());
		}
	}
	return tag_names;
}

int GameplayTagContainer::find_tag_index(const StringName& tag_name) const
{
	for (int i = 0; i < gameplay_tags.size(); i++)
	{
		const Ref<GameplayTag>& tag = gameplay_tags[i];
		if (tag.is_valid() && tag->get_tag_name() == tag_name) { return i; }
	}
	return -1;
}

void GameplayTagContainer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("add_tag", "tag"), &GameplayTagContainer::add_tag);
	ClassDB::bind_method(D_METHOD("add_tag_by_name", "tag_name"), &GameplayTagContainer::add_tag_by_name);
	ClassDB::bind_method(D_METHOD("remove_tag", "tag"), &GameplayTagContainer::remove_tag);
	ClassDB::bind_method(D_METHOD("has_tag", "tag_to_check"), &GameplayTagContainer::has_tag);
	ClassDB::bind_method(D_METHOD("has_tag_exact", "tag_to_check"), &GameplayTagContainer::has_tag_exact);
	ClassDB::bind_method(D_METHOD("has_any", "tag_container"), &GameplayTagContainer::has_any);
	ClassDB::bind_method(D_METHOD("has_all", "tag_container"), &GameplayTagContainer::has_all);
	ClassDB::bind_method(D_METHOD("has_all_exact", "tag_container"), &GameplayTagContainer::has_all_exact);
	ClassDB::bind_method(D_METHOD("get_length"), &GameplayTagContainer::get_length);
	ClassDB::bind_method(D_METHOD("get_tag", "index"), &GameplayTagContainer::get_tag);

	ClassDB::bind_method(D_METHOD("set_tags", "tag_names"), &GameplayTagContainer::set_tags);
	ClassDB::bind_method(D_METHOD("get_tags"), &GameplayTagContainer::get_tags);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "tags"), "set_tags", "get_tags");
}
}
