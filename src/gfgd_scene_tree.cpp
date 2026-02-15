#include "gfgd_scene_tree.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/window.hpp>

#include "game_instance.h"
#include "game_mode.h"
#include "level.h"
#include "game_mode_settings.h"
#include "gfgd_statics.h"

using namespace godot;

namespace GFGD
{
GFGDSceneTree::GFGDSceneTree()
{
	
}

GFGDSceneTree::~GFGDSceneTree()
{
	
}

void GFGDSceneTree::_initialize()
{
    StringName game_instance_script_path = ProjectSettings::get_singleton()->get_setting("application/game_framework/game_instance_script");
    Ref<Script> game_instance_script = cast_to<Script>(*ResourceLoader::get_singleton()->load(game_instance_script_path));
    CRASH_COND_MSG(game_instance_script.is_null(), "Load Game Instance failed. Please update \"application/game_framework/default_game_mode_settings\" option in project settings.");

    game_instance = GFGD::try_create_instance_from<GameInstance>(game_instance_script);
    CRASH_COND_MSG(game_instance, "Game Instance creation failed.");

    level = find_level();
    create_game_mode();

    if (level)
    {
        level->init_level(this);
    }
}

Level* GFGDSceneTree::find_level()
{
    TypedArray<Node> root_children = get_root()->get_children();
    for (int i = 0; i < root_children.size(); i++)
    {
        if (Level* found_level = cast_to<Level>(root_children[i]))
        {
            return found_level;
        }
    }

    return nullptr;
}

void GFGDSceneTree::create_game_mode()
{
    Ref<GameModeSettings> game_mode_settings;
    if (level && level->get_game_mode_settings_override().is_valid())
    {
        game_mode_settings = level->get_game_mode_settings_override();
    }
    else
    {
        StringName game_mode_settings_path = ProjectSettings::get_singleton()->get_setting("application/game_framework/default_game_mode_settings");
        game_mode_settings.reference_ptr(cast_to<GameModeSettings>(*ResourceLoader::get_singleton()->load(game_mode_settings_path)));
        CRASH_COND_MSG(game_mode_settings.is_null(), "Load Game Mode Settings failed. Please update \"application/game_framework/default_game_mode_settings\" option in project settings.");
    }

    if (game_mode)
    {
        game_mode->queue_free();
    }

    game_mode = GFGD::try_create_instance_from<GameMode>(game_mode_settings->get_game_mode_script());
    CRASH_COND_MSG(game_mode, "Game Mode creation failed.");
    game_mode->set_name("GameMode");
    game_mode->set_game_mode_settings(game_mode_settings);
    game_mode->init_game(this);
    get_root()->add_child(game_mode);
}

void GFGDSceneTree::open_level(const String& resource_path)
{
    if (level != nullptr)
    {
        level->queue_free();
    }

    Ref<PackedScene> level_packed_scene = ResourceLoader::get_singleton()->load<PackedScene>(resource_path);
    if (level_packed_scene.is_valid())
    {
        Node* level_node = level_packed_scene->instantiate();
        level = cast_to<Level>(level_node);
        if (level != nullptr)
        {
            create_game_mode();
            level->init_level(this);
            get_root()->add_child(level);
        }
    }
}

void GFGDSceneTree::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("set_game_instance", "instance"), &GFGDSceneTree::set_game_instance);
    ClassDB::bind_method(D_METHOD("get_game_instance"), &GFGDSceneTree::get_game_instance);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "game_instance", PROPERTY_HINT_RESOURCE_TYPE, "GameInstance"), "set_game_instance", "get_game_instance");

    ClassDB::bind_method(D_METHOD("set_game_mode", "mode"), &GFGDSceneTree::set_game_mode);
    ClassDB::bind_method(D_METHOD("get_game_mode"), &GFGDSceneTree::get_game_mode);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "game_mode", PROPERTY_HINT_RESOURCE_TYPE, "GameMode"), "set_game_mode", "get_game_mode");

    ClassDB::bind_method(D_METHOD("set_level", "level"), &GFGDSceneTree::set_level);
    ClassDB::bind_method(D_METHOD("get_level"), &GFGDSceneTree::get_level);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "level", PROPERTY_HINT_RESOURCE_TYPE, "Level"), "set_level", "get_level");

    ClassDB::bind_method(D_METHOD("find_level"), &GFGDSceneTree::find_level);
}
}