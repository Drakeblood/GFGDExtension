#include "framework/game_instance.h"
#include <godot_cpp/core/class_db.hpp>

#include "framework/gfgd_scene_tree.h"
#include "framework/local_player.h"

using namespace godot;

namespace GFGD
{
GameInstance::GameInstance()
{
	scene_tree = nullptr;
}

GameInstance::~GameInstance()
{

}

void GameInstance::init(GFGDSceneTree* in_scene_tree)
{
	scene_tree = in_scene_tree;
	GDVIRTUAL_CALL(_on_init, in_scene_tree);
}

void GameInstance::shutdown()
{
	GDVIRTUAL_CALL(_on_shutdown);

	for (int i = 0; i < local_players.size(); i++)
	{
		if (LocalPlayer* local_player = Object::cast_to<LocalPlayer>(local_players[i]))
		{
			memdelete(local_player);
		}
	}
	local_players.clear();
}

LocalPlayer* GameInstance::create_local_player(int device_slot)
{
	PackedInt32Array device_slots;
	if (device_slot != PlayerInput::DEVICE_SLOT_NONE)
	{
		device_slots.push_back(device_slot);
	}

	return create_local_player_with_slots(device_slots);
}

LocalPlayer* GameInstance::create_local_player_with_slots(const PackedInt32Array& device_slots)
{
	LocalPlayer* local_player = memnew(LocalPlayer);
	local_player->set_player_index(local_players.size());
	local_player->set_device_slots(device_slots);

	local_players.append(local_player);
	emit_signal("local_player_added", local_player);

	return local_player;
}

void GameInstance::remove_local_player(LocalPlayer* local_player)
{
	if (local_player == nullptr) { return; }

	const int index = local_players.find(local_player);
	if (index < 0) { return; }

	local_players.remove_at(index);
	emit_signal("local_player_removed", local_player);

	// Keep the indices dense so player 1 stays player 1 after player 0 leaves.
	for (int i = index; i < local_players.size(); i++)
	{
		if (LocalPlayer* shifted = Object::cast_to<LocalPlayer>(local_players[i]))
		{
			shifted->set_player_index(i);
		}
	}

	memdelete(local_player);
}

LocalPlayer* GameInstance::get_local_player(int index) const
{
	if (index < 0 || index >= local_players.size()) { return nullptr; }

	return Object::cast_to<LocalPlayer>(local_players[index]);
}

LocalPlayer* GameInstance::find_local_player_for_device_slot(int device_slot) const
{
	for (int i = 0; i < local_players.size(); i++)
	{
		LocalPlayer* local_player = Object::cast_to<LocalPlayer>(local_players[i]);
		if (local_player != nullptr && local_player->has_device_slot(device_slot))
		{
			return local_player;
		}
	}

	return nullptr;
}

void GameInstance::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_scene_tree"), &GameInstance::get_scene_tree);

	ClassDB::bind_method(D_METHOD("create_local_player", "device_slot"), &GameInstance::create_local_player);
	ClassDB::bind_method(D_METHOD("create_local_player_with_slots", "device_slots"), &GameInstance::create_local_player_with_slots);
	ClassDB::bind_method(D_METHOD("remove_local_player", "local_player"), &GameInstance::remove_local_player);
	ClassDB::bind_method(D_METHOD("get_local_player", "index"), &GameInstance::get_local_player);
	ClassDB::bind_method(D_METHOD("find_local_player_for_device_slot", "device_slot"), &GameInstance::find_local_player_for_device_slot);
	ClassDB::bind_method(D_METHOD("get_local_players"), &GameInstance::get_local_players);
	ClassDB::bind_method(D_METHOD("get_local_player_count"), &GameInstance::get_local_player_count);

	GDVIRTUAL_BIND(_on_init, "scene_tree");
	GDVIRTUAL_BIND(_on_shutdown);

	ADD_SIGNAL(MethodInfo("local_player_added", PropertyInfo(Variant::OBJECT, "local_player", PROPERTY_HINT_RESOURCE_TYPE, "LocalPlayer")));
	ADD_SIGNAL(MethodInfo("local_player_removed", PropertyInfo(Variant::OBJECT, "local_player", PROPERTY_HINT_RESOURCE_TYPE, "LocalPlayer")));
}
}
