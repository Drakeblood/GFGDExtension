#ifndef PROJECT_STATICS_H
#define PROJECT_STATICS_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/script.hpp>

using namespace godot;

namespace GFGD 
{
class GameInstance;
class GameModeBase;
class GameStateBase;
class Level;
class LocalPlayer;
class PlayerController;
class World;
class SaveGame;

class ProjectStatics : public Object
{
	GDCLASS(ProjectStatics, Object)

public:
	static void save_game(const String& slot_name, const Ref<SaveGame>& data, bool encrypt = true);
	// Pass the script of your SaveGame subclass to get that type back, filled in.
	// Without it the slot is read into a bare SaveGame, which has none of your
	// properties and therefore keeps none of your data.
	static Ref<SaveGame> load_game(const String& slot_name, bool encrypt = true, const Ref<Script>& save_game_script = Ref<Script>());

	static GameInstance* get_game_instance(World* world);
	static GameModeBase* get_game_mode(World* world);
	static GameStateBase* get_game_state(World* world);
	static Level* get_level(World* world);
	static void open_level(World* world, const String& resource_path);

	static PlayerController* get_first_player_controller(World* world);
	static PlayerController* get_player_controller(World* world, int index);
	static int get_player_controller_count(World* world);
	static LocalPlayer* get_local_player(World* world, int index);
	static int get_local_player_count(World* world);

protected:
	static void _bind_methods();
};

}

#endif