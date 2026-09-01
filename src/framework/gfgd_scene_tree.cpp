#include "framework/gfgd_scene_tree.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/window.hpp>

#include "framework/controller.h"
#include "framework/game_instance.h"
#include "framework/game_mode.h"
#include "framework/game_state.h"
#include "framework/input_router.h"
#include "framework/level.h"
#include "framework/local_player.h"
#include "framework/game_mode_settings.h"
#include "framework/player_controller.h"
#include "framework/player_input.h"
#include "gameplay_tags/gameplay_tags_manager.h"
#include "core/gfgd_statics.h"

using namespace godot;

namespace GFGD
{
GFGDSceneTree::GFGDSceneTree()
{
	game_instance = nullptr;
	game_mode = nullptr;
	game_state = nullptr;
	level = nullptr;
	input_router = nullptr;
}

GFGDSceneTree::~GFGDSceneTree()
{

}

void GFGDSceneTree::_initialize()
{
	GameplayTagsManager::get_singleton();

	create_game_instance();
	game_instance->init(this);

	// The root only enters the tree after MainLoop::initialize() returns, so
	// defer level discovery and game mode creation to the first frame, when
	// the main scene is inside the tree and its _ready callbacks have run.
	connect("process_frame", callable_mp(this, &GFGDSceneTree::initialize_game), CONNECT_ONE_SHOT);
}

void GFGDSceneTree::initialize_game()
{
	// The main scene is the level. Godot has already added it by now, so its
	// nodes have run _ready before the game mode is created - unlike a level
	// opened with open_level(), where _init_game sees a level that has not
	// started yet.
	create_input_router();

	level = find_level();

	// The game state is world scope, not level scope, so it can be added right
	// away - login() needs somewhere to attach player states to.
	create_game_state();
	get_root()->add_child(game_state);
	game_state->init_game_state(this);

	create_game_mode();
	get_root()->add_child(game_mode);

	if (level)
	{
		level->init_level(this);
	}
}

void GFGDSceneTree::_finalize()
{
	if (game_instance != nullptr)
	{
		game_instance->shutdown();
		memdelete(game_instance);
		game_instance = nullptr;
	}

	// game_mode, game_state and input_router are owned by the tree and may
	// already be gone by now, so nothing is freed here.
}

void GFGDSceneTree::create_game_instance()
{
	String game_instance_script_path = ProjectSettings::get_singleton()->get_setting("application/game_framework/game_instance_script", String());
	if (!game_instance_script_path.is_empty())
	{
		Ref<Script> game_instance_script = ResourceLoader::get_singleton()->load(game_instance_script_path);
		if (game_instance_script.is_valid())
		{
			game_instance = GFGD::try_create_instance_from<GameInstance>(game_instance_script);
		}

		if (game_instance == nullptr)
		{
			WARN_PRINT("GFGD: Could not create a GameInstance from \"application/game_framework/game_instance_script\". Falling back to the default GameInstance.");
		}
	}

	if (game_instance == nullptr)
	{
		game_instance = memnew(GameInstance);
	}
}

void GFGDSceneTree::create_input_router()
{
	if (input_router != nullptr) { return; }

	input_router = memnew(InputRouter);
	input_router->set_name("InputRouter");
	input_router->set_gfgd_scene_tree(this);
	get_root()->add_child(input_router);
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

namespace
{
Ref<GameModeSettings> resolve_game_mode_settings(Level* level)
{
	Ref<GameModeSettings> game_mode_settings;
	if (level && level->get_game_mode_settings_override().is_valid())
	{
		game_mode_settings = level->get_game_mode_settings_override();
	}
	else
	{
		String game_mode_settings_path = ProjectSettings::get_singleton()->get_setting("application/game_framework/default_game_mode_settings", String());
		if (!game_mode_settings_path.is_empty())
		{
			game_mode_settings = ResourceLoader::get_singleton()->load(game_mode_settings_path);
		}

		if (game_mode_settings.is_null())
		{
			WARN_PRINT("GFGD: \"application/game_framework/default_game_mode_settings\" is not set or failed to load. Using empty GameModeSettings.");
		}
	}

	if (game_mode_settings.is_null())
	{
		game_mode_settings.instantiate();
	}

	return game_mode_settings;
}
}

void GFGDSceneTree::create_game_state()
{
	Ref<GameModeSettings> game_mode_settings = resolve_game_mode_settings(level);

	if (game_mode_settings->get_game_state_scene().is_valid())
	{
		Node* game_state_node = game_mode_settings->get_game_state_scene()->instantiate();
		game_state = Object::cast_to<GameState>(game_state_node);
		if (game_state == nullptr && game_state_node != nullptr)
		{
			WARN_PRINT("GFGD: game_state_scene root is not a GameState. Falling back to the default GameState.");
			game_state_node->queue_free();
		}
	}

	if (game_state == nullptr)
	{
		game_state = memnew(GameState);
	}

	game_state->set_name("GameState");
}

void GFGDSceneTree::create_game_mode()
{
	Ref<GameModeSettings> game_mode_settings = resolve_game_mode_settings(level);

	if (game_mode_settings->get_game_mode_script().is_valid())
	{
		game_mode = GFGD::try_create_instance_from<GameMode>(game_mode_settings->get_game_mode_script());
	}
	if (game_mode == nullptr)
	{
		if (game_mode_settings->get_game_mode_script().is_valid())
		{
			WARN_PRINT("GFGD: Could not create a GameMode from the GameModeSettings script. Falling back to the default GameMode.");
		}
		game_mode = memnew(GameMode);
	}

	game_mode->set_name("GameMode");
	game_mode->set_game_mode_settings(game_mode_settings);

	// init_game runs with the game mode still outside the scene tree, so any
	// player it spawns is outside it too and enters together with the level.
	// The caller adds the game mode once the level is in, which makes its
	// _ready the point where everything - level, nodes, player - is live.
	game_mode->init_game(this);
}

void GFGDSceneTree::destroy_level_nodes()
{
	// remove_child before queue_free, not queue_free alone: queue_free is deferred
	// to the end of the frame, so the old nodes would still be children of the
	// root - keeping their names, still running _process, and still listed as
	// controllers - while the new level is being built. remove_child runs
	// _exit_tree immediately, which is what unregisters the old controllers.
	if (game_mode != nullptr)
	{
		if (game_mode->get_parent() != nullptr)
		{
			game_mode->get_parent()->remove_child(game_mode);
		}
		game_mode->queue_free();
		game_mode = nullptr;
	}

	if (game_state != nullptr)
	{
		if (game_state->get_parent() != nullptr)
		{
			game_state->get_parent()->remove_child(game_state);
		}
		game_state->queue_free();
		game_state = nullptr;
	}

	if (level != nullptr)
	{
		if (level->get_parent() != nullptr)
		{
			level->get_parent()->remove_child(level);
		}
		level->queue_free();
		level = nullptr;
	}

	controller_list.clear();
	player_controller_list.clear();
}

void GFGDSceneTree::open_level(const String& resource_path)
{
	Ref<PackedScene> level_packed_scene = ResourceLoader::get_singleton()->load(resource_path);
	if (level_packed_scene.is_null())
	{
		ERR_PRINT(vformat("GFGD: open_level failed to load \"%s\".", resource_path));
		return;
	}

	Node* level_node = level_packed_scene->instantiate();
	Level* new_level = cast_to<Level>(level_node);
	if (new_level == nullptr)
	{
		ERR_PRINT(vformat("GFGD: open_level scene root of \"%s\" is not a Level node.", resource_path));
		if (level_node != nullptr)
		{
			level_node->queue_free();
		}
		return;
	}

	// Local players outlive the level, but their controllers do not. Cut the link
	// before the old game mode is freed so nothing is left pointing at a dead
	// controller, and release whatever was being held on the old level's devices.
	if (game_instance != nullptr)
	{
		for (int i = 0; i < game_instance->get_local_player_count(); i++)
		{
			LocalPlayer* local_player = game_instance->get_local_player(i);
			if (local_player == nullptr) { continue; }

			local_player->set_player_controller(nullptr);
			local_player->set_viewport_override(nullptr);
			if (PlayerInput* player_input = local_player->get_player_input())
			{
				player_input->reset_action_states();
				player_input->refresh_action_cache();
			}
		}
	}

	destroy_level_nodes();

	// An instantiated scene is a complete node tree that is not in the scene tree
	// yet, so nothing in it has run _enter_tree or _ready. Creating the game mode
	// here - before add_child - is what gives _init_game its footing: the level
	// and every node in it are reachable, but none has started. Anything the game
	// mode spawns into the level starts together with it.
	level = new_level;

	create_game_state();
	get_root()->add_child(game_state);
	game_state->init_game_state(this);

	create_game_mode();

	get_root()->add_child(level);
	get_root()->add_child(game_mode);
	level->init_level(this);
}

void GFGDSceneTree::register_controller(Controller* controller)
{
	if (controller == nullptr) { return; }
	if (controller_list.has(controller)) { return; }

	controller_list.append(controller);

	if (Object::cast_to<PlayerController>(controller) != nullptr)
	{
		player_controller_list.append(controller);
	}
}

void GFGDSceneTree::unregister_controller(Controller* controller)
{
	if (controller == nullptr) { return; }

	controller_list.erase(controller);
	player_controller_list.erase(controller);
}

void GFGDSceneTree::prune_controller_lists()
{
	// Self-unregistration in _exit_tree normally keeps these clean; this is the
	// belt and braces for a controller freed some other way.
	for (int i = controller_list.size() - 1; i >= 0; i--)
	{
		if (controller_list[i].get_validated_object() == nullptr)
		{
			controller_list.remove_at(i);
		}
	}

	for (int i = player_controller_list.size() - 1; i >= 0; i--)
	{
		if (player_controller_list[i].get_validated_object() == nullptr)
		{
			player_controller_list.remove_at(i);
		}
	}
}

Array GFGDSceneTree::get_controllers() const
{
	const_cast<GFGDSceneTree*>(this)->prune_controller_lists();
	return controller_list;
}

Array GFGDSceneTree::get_player_controllers() const
{
	const_cast<GFGDSceneTree*>(this)->prune_controller_lists();
	return player_controller_list;
}

PlayerController* GFGDSceneTree::get_first_player_controller() const
{
	return get_player_controller_at(0);
}

PlayerController* GFGDSceneTree::get_player_controller_at(int index) const
{
	const_cast<GFGDSceneTree*>(this)->prune_controller_lists();
	if (index < 0 || index >= player_controller_list.size()) { return nullptr; }

	return Object::cast_to<PlayerController>(player_controller_list[index].get_validated_object());
}

int GFGDSceneTree::get_player_controller_count() const
{
	const_cast<GFGDSceneTree*>(this)->prune_controller_lists();
	return player_controller_list.size();
}

LocalPlayer* GFGDSceneTree::create_local_player(int device_slot)
{
	return game_instance != nullptr ? game_instance->create_local_player(device_slot) : nullptr;
}

LocalPlayer* GFGDSceneTree::create_default_local_player()
{
	if (game_instance == nullptr) { return nullptr; }

	// DEVICE_SLOT_ALL is what makes an untouched project behave exactly as it did
	// before per-player input existed: every device drives player 0, and
	// PlayerInput forwards every query straight to the Input singleton.
	return game_instance->create_local_player(PlayerInput::DEVICE_SLOT_ALL);
}

LocalPlayer* GFGDSceneTree::get_local_player(int index) const
{
	return game_instance != nullptr ? game_instance->get_local_player(index) : nullptr;
}

int GFGDSceneTree::get_local_player_count() const
{
	return game_instance != nullptr ? game_instance->get_local_player_count() : 0;
}

bool GFGDSceneTree::has_authority() const
{
	Ref<MultiplayerAPI> multiplayer = get_multiplayer();
	if (multiplayer.is_null()) { return true; }
	if (!multiplayer->has_multiplayer_peer()) { return true; }

	return multiplayer->is_server();
}

void GFGDSceneTree::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_game_instance"), &GFGDSceneTree::get_game_instance);
	ClassDB::bind_method(D_METHOD("get_game_mode"), &GFGDSceneTree::get_game_mode);
	ClassDB::bind_method(D_METHOD("get_game_state"), &GFGDSceneTree::get_game_state);
	ClassDB::bind_method(D_METHOD("get_level"), &GFGDSceneTree::get_level);
	ClassDB::bind_method(D_METHOD("get_input_router"), &GFGDSceneTree::get_input_router);

	ClassDB::bind_method(D_METHOD("find_level"), &GFGDSceneTree::find_level);
	ClassDB::bind_method(D_METHOD("open_level", "resource_path"), &GFGDSceneTree::open_level);

	ClassDB::bind_method(D_METHOD("register_controller", "controller"), &GFGDSceneTree::register_controller);
	ClassDB::bind_method(D_METHOD("unregister_controller", "controller"), &GFGDSceneTree::unregister_controller);
	ClassDB::bind_method(D_METHOD("get_controllers"), &GFGDSceneTree::get_controllers);
	ClassDB::bind_method(D_METHOD("get_player_controllers"), &GFGDSceneTree::get_player_controllers);
	ClassDB::bind_method(D_METHOD("get_first_player_controller"), &GFGDSceneTree::get_first_player_controller);
	ClassDB::bind_method(D_METHOD("get_player_controller_at", "index"), &GFGDSceneTree::get_player_controller_at);
	ClassDB::bind_method(D_METHOD("get_player_controller_count"), &GFGDSceneTree::get_player_controller_count);

	ClassDB::bind_method(D_METHOD("create_local_player", "device_slot"), &GFGDSceneTree::create_local_player);
	ClassDB::bind_method(D_METHOD("create_default_local_player"), &GFGDSceneTree::create_default_local_player);
	ClassDB::bind_method(D_METHOD("get_local_player", "index"), &GFGDSceneTree::get_local_player);
	ClassDB::bind_method(D_METHOD("get_local_player_count"), &GFGDSceneTree::get_local_player_count);

	ClassDB::bind_method(D_METHOD("has_authority"), &GFGDSceneTree::has_authority);
}
}
