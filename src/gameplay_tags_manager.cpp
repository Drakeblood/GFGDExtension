#include "gameplay_tags_manager.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/file_access.hpp>

#include "gameplay_tag.h"

using namespace godot;

namespace GFGD
{
GameplayTagsManager* GameplayTagsManager::instance = nullptr;

GameplayTagsManager* GameplayTagsManager::get_singleton()
{
	if (instance == nullptr)
	{
		instance = memnew(GameplayTagsManager());
	}
	return instance;
}

void GameplayTagsManager::destroy_singleton()
{
	if (instance != nullptr)
	{
		memdelete(instance);
		instance = nullptr;
	}
}

GameplayTagsManager::GameplayTagsManager()
{
	initialize_tags();
}

GameplayTagsManager::~GameplayTagsManager()
{
	
}

Ref<GameplayTag> GameplayTagsManager::get_tag(const StringName& tag_name) const
{
	Array keys = tags.get_keys();
	for (int i = 0; i < keys.size(); i++)
	{
		Ref<GameplayTag> tag = keys[i];
		if (tag.is_valid() && tag->get_tag_name() == tag_name)
		{
			return tag;
		}
	}
	return Ref<GameplayTag>();
}

Array GameplayTagsManager::get_separated_tag(const Ref<GameplayTag>& tag) const
{
	if (tags_with_sub_tags.has(tag))
	{
		return tags_with_sub_tags[tag];
	}
	return Array();
}

void GameplayTagsManager::initialize_tags()
{
	Array gameplay_tags_files_paths = ProjectSettings::get_singleton()->get_setting("application/game_framework/gameplay_tags_files");
	
	for (int i = 0; i < gameplay_tags_files_paths.size(); i++)
	{
		String file_path = gameplay_tags_files_paths[i];
		Ref<FileAccess> file = FileAccess::open(file_path, FileAccess::ModeFlags::READ);
		if (file.is_null()) continue;

		while (file->get_position() < file->get_length())
		{
			String line = file->get_line();
			int description_separator = line.find_char('-');

			String tag_name;
			if (description_separator != -1)
			{
				tag_name = line.substr(0, description_separator - 1);
			}
			else
			{
				tag_name = line;
			}

			if (!tag_name.is_empty())
			{
				Array parent_tags;
				String current_tag = tag_name;
				while (true)
				{
					int last_dot = current_tag.rfind_char('.');
					if (last_dot == -1) break;
					
					current_tag = current_tag.substr(0, last_dot);
					Ref<GameplayTag> parent_tag = memnew(GameplayTag(current_tag));
					parent_tags.push_back(parent_tag);
				}

				Ref<GameplayTag> gameplay_tag = memnew(GameplayTag(tag_name));
				tags.insert(gameplay_tag);
				tags_with_sub_tags[gameplay_tag] = parent_tags;
			}
		}

		file->close();
	}
}

void GameplayTagsManager::_bind_methods()
{
	ClassDB::bind_static_method("GameplayTagsManager", D_METHOD("get_singleton"), &GameplayTagsManager::get_singleton);
	ClassDB::bind_static_method("GameplayTagsManager", D_METHOD("destroy_singleton"), &GameplayTagsManager::destroy_singleton);
	
	ClassDB::bind_method(D_METHOD("get_tag", "tag_name"), &GameplayTagsManager::get_tag);
	ClassDB::bind_method(D_METHOD("get_separated_tag", "tag"), &GameplayTagsManager::get_separated_tag);
}
}