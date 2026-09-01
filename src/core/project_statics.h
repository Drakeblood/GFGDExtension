#ifndef PROJECT_STATICS_H
#define PROJECT_STATICS_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>

using namespace godot;

namespace GFGD 
{
class GameInstance;
class GameMode;
class GameState;
class Level;
class LocalPlayer;
class PlayerController;
class GFGDSceneTree;
class SaveGame;

class ProjectStatics : public Object
{
	GDCLASS(ProjectStatics, Object)

public:
	static void save_game(const String& slot_name, const Ref<SaveGame>& data, bool encrypt = true);
	static Ref<SaveGame> load_game(const String& slot_name, bool encrypt = true);

	static GameInstance* get_game_instance(GFGDSceneTree* scene_tree);
	static GameMode* get_game_mode(GFGDSceneTree* scene_tree);
	static GameState* get_game_state(GFGDSceneTree* scene_tree);
	static Level* get_level(GFGDSceneTree* scene_tree);
	static void open_level(GFGDSceneTree* scene_tree, const String& resource_path);

	static PlayerController* get_first_player_controller(GFGDSceneTree* scene_tree);
	static PlayerController* get_player_controller(GFGDSceneTree* scene_tree, int index);
	static int get_player_controller_count(GFGDSceneTree* scene_tree);
	static LocalPlayer* get_local_player(GFGDSceneTree* scene_tree, int index);
	static int get_local_player_count(GFGDSceneTree* scene_tree);

protected:
	static void _bind_methods();
};

}

#endif