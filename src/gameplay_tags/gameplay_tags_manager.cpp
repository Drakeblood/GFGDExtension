#include "gameplay_tags/gameplay_tags_manager.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

#include "gameplay_tags/gameplay_tag.h"
#include "gameplay_tags/gameplay_tag_table.h"

using namespace godot;

namespace GFGD
{
namespace
{
const char* TAG_TABLES_SETTING = "application/game_framework/gameplay_tag_tables";
}

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

bool GameplayTagsManager::has_tag(const StringName& tag_name) const
{
	return tags.has(tag_name);
}

Ref<GameplayTag> GameplayTagsManager::get_tag(const StringName& tag_name) const
{
	const TagEntry* found = tags.getptr(tag_name);
	if (found == nullptr)
	{
		WARN_PRINT(vformat("GFGD: Gameplay tag \"%s\" is not registered.", tag_name));
		return Ref<GameplayTag>();
	}
	return found->tag;
}

Ref<GameplayTag> GameplayTagsManager::request_tag(const StringName& tag_name)
{
	const TagEntry* found = tags.getptr(tag_name);
	if (found != nullptr)
	{
		return found->tag;
	}

	if (!is_valid_tag_name(tag_name))
	{
		WARN_PRINT(vformat("GFGD: Cannot request malformed gameplay tag \"%s\".", tag_name));
		return Ref<GameplayTag>();
	}

	register_tag_with_parents(tag_name, nullptr);
	tags[tag_name].source_kind = TAG_SOURCE_RUNTIME;

#ifdef DEBUG_ENABLED
	// Loud in editor and debug builds only: a shipped game may legitimately
	// build tag names from data, but during development this nearly always means
	// a typo or a tag that was dropped from its table.
	WARN_PRINT(vformat("GFGD: Gameplay tag \"%s\" is not declared in any gameplay tag table and was registered at runtime.", tag_name));
#endif

	return tags[tag_name].tag;
}

TypedArray<GameplayTag> GameplayTagsManager::get_separated_tag(const Ref<GameplayTag>& tag)
{
	TypedArray<GameplayTag> separated;
	if (tag.is_null()) { return separated; }

	String full_name = tag->get_tag_name();
	int search_from = 0;
	while (true)
	{
		int dot = full_name.find(".", search_from);
		if (dot == -1) { break; }

		separated.push_back(request_tag(full_name.substr(0, dot)));
		search_from = dot + 1;
	}
	separated.push_back(request_tag(full_name));

	return separated;
}

PackedStringArray GameplayTagsManager::get_all_tag_names() const
{
	PackedStringArray names;
	for (const KeyValue<StringName, TagEntry>& entry : tags)
	{
		names.push_back(entry.key);
	}
	names.sort();
	return names;
}

GameplayTagsManager::TagSourceKind GameplayTagsManager::get_tag_source_kind(const StringName& tag_name) const
{
	const TagEntry* found = tags.getptr(tag_name);
	return (found != nullptr) ? found->source_kind : TAG_SOURCE_RUNTIME;
}

String GameplayTagsManager::get_tag_source_path(const StringName& tag_name) const
{
	const TagEntry* found = tags.getptr(tag_name);
	return (found != nullptr) ? found->source_path : String();
}

String GameplayTagsManager::get_tag_description(const StringName& tag_name) const
{
	const TagEntry* found = tags.getptr(tag_name);
	return (found != nullptr) ? found->description : String();
}

void GameplayTagsManager::initialize_tags()
{
	// Keep the outgoing entries so tags that survive the rebuild reuse their
	// instance; refs already handed out stay the canonical ones.
	const HashMap<StringName, TagEntry> previous_tags = tags;
	tags.clear();
	loaded_table_paths.clear();

	PackedStringArray table_paths = ProjectSettings::get_singleton()->get_setting(TAG_TABLES_SETTING, PackedStringArray());

	for (int i = 0; i < table_paths.size(); i++)
	{
		const String table_path = String(table_paths[i]).strip_edges();
		if (table_path.is_empty()) { continue; }

		if (loaded_table_paths.find(table_path) != -1)
		{
			WARN_PRINT(vformat("GFGD: Gameplay tag table \"%s\" is registered more than once; ignoring the repeat.", table_path));
			continue;
		}

		Ref<Resource> resource = ResourceLoader::get_singleton()->load(table_path);
		if (resource.is_null())
		{
			WARN_PRINT(vformat("GFGD: Could not load gameplay tag table \"%s\".", table_path));
			continue;
		}

		Ref<GameplayTagTable> table = Object::cast_to<GameplayTagTable>(resource.ptr());
		if (table.is_null())
		{
			WARN_PRINT(vformat("GFGD: Resource \"%s\" is not a GameplayTagTable.", table_path));
			continue;
		}

		loaded_table_paths.push_back(table_path);

		// Tables are walked in setting order and the first declaration wins, so
		// a table listed early cannot be silently overridden by a later one.
		const PackedStringArray tag_names = table->get_tag_names();
		for (int j = 0; j < tag_names.size(); j++)
		{
			const String tag_name = tag_names[j];
			declare_tag(tag_name, table_path, table->get_description(tag_name), &previous_tags);
		}
	}
}

bool GameplayTagsManager::is_valid_tag_name(const String& tag_name)
{
	if (tag_name.is_empty()) { return false; }
	if (tag_name != tag_name.strip_edges()) { return false; }
	if (tag_name.begins_with(".") || tag_name.ends_with(".")) { return false; }
	if (tag_name.contains("..")) { return false; }
	if (tag_name.contains(" ")) { return false; }
	return true;
}

void GameplayTagsManager::declare_tag(const String& tag_name, const String& source_path, const String& description, const HashMap<StringName, TagEntry>* previous_tags)
{
	if (!is_valid_tag_name(tag_name))
	{
		WARN_PRINT(vformat("GFGD: Ignoring malformed gameplay tag \"%s\" declared in \"%s\".", tag_name, source_path));
		return;
	}

	register_tag_with_parents(tag_name, previous_tags);

	TagEntry& entry = tags[tag_name];
	if (entry.source_kind == TAG_SOURCE_TABLE)
	{
		WARN_PRINT(vformat("GFGD: Gameplay tag \"%s\" is declared in both \"%s\" and \"%s\"; keeping the first declaration.", tag_name, entry.source_path, source_path));
		return;
	}

	// Anything else the entry could be is an ancestor implied by a longer tag,
	// which several tables may imply at once - declaring it is not a conflict,
	// the table just takes ownership of it.
	entry.source_kind = TAG_SOURCE_TABLE;
	entry.source_path = source_path;
	entry.description = description;
}

void GameplayTagsManager::register_tag_with_parents(const String& tag_name, const HashMap<StringName, TagEntry>* previous_tags)
{
	String current_tag = tag_name;
	while (!current_tag.is_empty())
	{
		insert_implicit_tag(current_tag, previous_tags);

		int last_dot = current_tag.rfind(".");
		if (last_dot == -1) { break; }
		current_tag = current_tag.substr(0, last_dot);
	}
}

void GameplayTagsManager::insert_implicit_tag(const StringName& tag_name, const HashMap<StringName, TagEntry>* previous_tags)
{
	if (tags.has(tag_name)) { return; }

	TagEntry entry;
	const TagEntry* previous = (previous_tags != nullptr) ? previous_tags->getptr(tag_name) : nullptr;
	entry.tag = (previous != nullptr && previous->tag.is_valid()) ? previous->tag : Ref<GameplayTag>(memnew(GameplayTag(tag_name)));
	entry.source_kind = TAG_SOURCE_IMPLICIT;

	tags.insert(tag_name, entry);
}

void GameplayTagsManager::_bind_methods()
{
	ClassDB::bind_static_method("GameplayTagsManager", D_METHOD("get_singleton"), &GameplayTagsManager::get_singleton);
	ClassDB::bind_static_method("GameplayTagsManager", D_METHOD("destroy_singleton"), &GameplayTagsManager::destroy_singleton);

	ClassDB::bind_method(D_METHOD("has_tag", "tag_name"), &GameplayTagsManager::has_tag);
	ClassDB::bind_method(D_METHOD("get_tag", "tag_name"), &GameplayTagsManager::get_tag);
	ClassDB::bind_method(D_METHOD("request_tag", "tag_name"), &GameplayTagsManager::request_tag);
	ClassDB::bind_method(D_METHOD("get_separated_tag", "tag"), &GameplayTagsManager::get_separated_tag);
	ClassDB::bind_method(D_METHOD("get_all_tag_names"), &GameplayTagsManager::get_all_tag_names);

	ClassDB::bind_method(D_METHOD("get_tag_source_kind", "tag_name"), &GameplayTagsManager::get_tag_source_kind);
	ClassDB::bind_method(D_METHOD("get_tag_source_path", "tag_name"), &GameplayTagsManager::get_tag_source_path);
	ClassDB::bind_method(D_METHOD("get_tag_description", "tag_name"), &GameplayTagsManager::get_tag_description);
	ClassDB::bind_method(D_METHOD("get_loaded_table_paths"), &GameplayTagsManager::get_loaded_table_paths);

	ClassDB::bind_method(D_METHOD("initialize_tags"), &GameplayTagsManager::initialize_tags);
	ClassDB::bind_static_method("GameplayTagsManager", D_METHOD("is_valid_tag_name", "tag_name"), &GameplayTagsManager::is_valid_tag_name);

	BIND_ENUM_CONSTANT(TAG_SOURCE_TABLE);
	BIND_ENUM_CONSTANT(TAG_SOURCE_IMPLICIT);
	BIND_ENUM_CONSTANT(TAG_SOURCE_RUNTIME);
}
}
