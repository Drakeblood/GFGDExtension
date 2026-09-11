#ifndef PROJECT_STATICS_H
#define PROJECT_STATICS_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/binder_common.hpp>
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
	// Why the last load_game() returned what it did. Only LOAD_NOT_FOUND means
	// "there is no save yet"; every other failure means a file exists that the
	// player would lose if the caller started over and saved on top of it.
	enum LoadResult
	{
		LOAD_OK = 0,
		LOAD_NOT_FOUND = 1,
		// The file is there and could not be opened or decrypted. The usual
		// cause is a changed save_encryption_key, not corruption.
		LOAD_UNREADABLE = 2,
		// It was read, but the JSON did not parse or SaveGame::from_json
		// rejected it - which includes a game's own migration code saying no.
		LOAD_INVALID_DATA = 3,
		// save_game_script does not extend SaveGame. The caller is wrong, the
		// file is fine.
		LOAD_BAD_SCRIPT = 4,
	};

	// Refuses to write to a slot that failed to load this session for any reason
	// other than LOAD_NOT_FOUND; see is_slot_write_blocked(). Returns OK, or the
	// error that stopped it.
	static Error save_game(const String& slot_name, const Ref<SaveGame>& data, bool encrypt = true);
	// Pass the script of your SaveGame subclass to get that type back, filled in.
	// Without it the slot is read into a bare SaveGame, which has none of your
	// properties and therefore keeps none of your data.
	static Ref<SaveGame> load_game(const String& slot_name, bool encrypt = true, const Ref<Script>& save_game_script = Ref<Script>());

	// Whether the slot file exists at all, without reading it.
	static bool has_save_game(const String& slot_name);
	// The outcome of the most recent load_game() call, whichever slot it was for.
	static LoadResult get_last_load_result();
	// The outcome the given slot last produced, or LOAD_OK if it was never read.
	static LoadResult get_slot_load_result(const String& slot_name);
	// True while save_game() will refuse this slot, because reading it failed
	// with something other than LOAD_NOT_FOUND.
	static bool is_slot_write_blocked(const String& slot_name);
	// Deliberately discard that protection: the next save_game() overwrites the
	// slot. For a game that has told the player their save is unreadable and had
	// them choose to start over.
	static void clear_load_failure(const String& slot_name);

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

VARIANT_ENUM_CAST(GFGD::ProjectStatics::LoadResult);

#endif