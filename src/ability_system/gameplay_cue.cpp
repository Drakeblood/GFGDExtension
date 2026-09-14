#include "ability_system/gameplay_cue.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
namespace
{
const char* CUE_TABLES_SETTING = "application/game_framework/gameplay_cue_tables";
}

// --- GameplayCueNotify ---------------------------------------------------------

void GameplayCueNotify::execute(Node* target, const Dictionary& parameters)
{
	GDVIRTUAL_CALL(_on_execute, target, parameters);
}

void GameplayCueNotify::on_active(Node* target, const Dictionary& parameters)
{
	GDVIRTUAL_CALL(_on_active, target, parameters);
}

void GameplayCueNotify::on_removed(Node* target, const Dictionary& parameters)
{
	GDVIRTUAL_CALL(_on_removed, target, parameters);
}

void GameplayCueNotify::_bind_methods()
{
	GDVIRTUAL_BIND(_on_execute, "target", "parameters");
	GDVIRTUAL_BIND(_on_active, "target", "parameters");
	GDVIRTUAL_BIND(_on_removed, "target", "parameters");
}

// --- GameplayCueTable ----------------------------------------------------------

void GameplayCueTable::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_cues"), &GameplayCueTable::get_cues);
	ClassDB::bind_method(D_METHOD("set_cues", "value"), &GameplayCueTable::set_cues);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "cues", PROPERTY_HINT_DICTIONARY_TYPE, "StringName;Resource"), "set_cues", "get_cues");
}

// --- GameplayCueManager ----------------------------------------------------------

GameplayCueManager* GameplayCueManager::instance = nullptr;

GameplayCueManager* GameplayCueManager::get_singleton()
{
	if (instance == nullptr)
	{
		instance = memnew(GameplayCueManager());
	}
	return instance;
}

void GameplayCueManager::destroy_singleton()
{
	if (instance != nullptr)
	{
		memdelete(instance);
		instance = nullptr;
	}
}

GameplayCueManager::GameplayCueManager()
{
	loaded = false;
}

void GameplayCueManager::load_project_tables()
{
	// Lazily, on the first cue: the project settings and the resource loader are
	// not ready while the library itself is still loading.
	if (loaded) { return; }
	loaded = true;

	const PackedStringArray paths = ProjectSettings::get_singleton()->get_setting(CUE_TABLES_SETTING, PackedStringArray());
	for (int i = 0; i < paths.size(); i++)
	{
		const Ref<GameplayCueTable> table = ResourceLoader::get_singleton()->load(paths[i]);
		if (table.is_null())
		{
			WARN_PRINT(vformat("GFGD: \"%s\" in %s is not a GameplayCueTable; its cues will not play.", paths[i], CUE_TABLES_SETTING));
			continue;
		}
		add_table(table);
	}
}

void GameplayCueManager::add_table(const Ref<GameplayCueTable>& table)
{
	if (table.is_null()) { return; }

	handlers.merge(table->get_cues(), true);
	resolved.clear();
}

void GameplayCueManager::clear()
{
	handlers.clear();
	resolved.clear();
	loaded = true;
}

Ref<Resource> GameplayCueManager::find_handler(const StringName& cue_tag)
{
	load_project_tables();

	if (const Variant* cached = resolved.getptr(cue_tag))
	{
		return *cached;
	}

	Ref<Resource> handler;
	String name = cue_tag;
	while (!name.is_empty())
	{
		if (handlers.has(StringName(name)))
		{
			handler = handlers[StringName(name)];
			break;
		}

		const int dot = name.rfind(".");
		name = dot > 0 ? name.substr(0, dot) : String();
	}

	resolved[cue_tag] = handler;
	return handler;
}

void GameplayCueManager::_bind_methods()
{
	ClassDB::bind_static_method("GameplayCueManager", D_METHOD("get_singleton"), &GameplayCueManager::get_singleton);
	ClassDB::bind_method(D_METHOD("find_handler", "cue_tag"), &GameplayCueManager::find_handler);
	ClassDB::bind_method(D_METHOD("add_table", "table"), &GameplayCueManager::add_table);
	ClassDB::bind_method(D_METHOD("clear"), &GameplayCueManager::clear);
}
}
