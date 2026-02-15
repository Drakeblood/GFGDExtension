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
class Level;
class GFGDSceneTree;
class SaveGame;

class ProjectStatics : public Object
{
	GDCLASS(ProjectStatics, Object)

public:
	static const String USER_LOCATION;
	static const String SAVES_FOLDER;
	static const String SAVES_LOCATION;
	static const String SAVE_GAME_ENCRYPTION_KEY;

	static void save_game(const String& slot_name, const Ref<SaveGame>& data, bool encrypt = true);
	static Ref<SaveGame> load_game(const String& slot_name, bool encrypt = true);

	static GameInstance* get_game_instance(GFGDSceneTree* scene_tree);
	static GameMode* get_game_mode(GFGDSceneTree* scene_tree);
	static Level* get_level(GFGDSceneTree* scene_tree);
	static void open_level(GFGDSceneTree* scene_tree, const String& resource_path);

protected:
	static void _bind_methods();
};

}

#endif