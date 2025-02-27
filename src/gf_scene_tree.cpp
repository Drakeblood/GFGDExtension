#include "gf_scene_tree.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/gd_script.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

using namespace godot;

GFSceneTree::GFSceneTree()
{
	
}

GFSceneTree::~GFSceneTree()
{
	
}

void GFSceneTree::_initialize()
{
    StringName gameInstanceScriptPath = ProjectSettings::get_singleton()->get_setting("application/game_framework/game_instance_script");
    Ref<Script> gameInstanceScript = cast_to<Script>(*ResourceLoader::get_singleton()->load(gameInstanceScriptPath));

    if (Ref<GDScript> gdScript = cast_to<GDScript>(*gameInstanceScript); gdScript.is_valid())
    {
        gameInstance = cast_to<GameInstance>((Object*)gdScript->new_());
    }
}

void GFSceneTree::_bind_methods()
{

}