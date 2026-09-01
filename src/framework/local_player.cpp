#include "framework/local_player.h"
#include <godot_cpp/classes/camera2d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "framework/player_controller.h"

using namespace godot;

namespace GFGD
{
LocalPlayer::LocalPlayer()
{
	player_index = 0;
	viewport = nullptr;
	player_controller = nullptr;

	player_input = memnew(PlayerInput);
}

LocalPlayer::~LocalPlayer()
{
	if (player_input != nullptr)
	{
		memdelete(player_input);
		player_input = nullptr;
	}
}

PackedInt32Array LocalPlayer::get_device_slots() const
{
	return player_input->get_device_slots();
}

void LocalPlayer::set_device_slots(const PackedInt32Array& value)
{
	player_input->set_device_slots(value);
}

void LocalPlayer::add_device_slot(int device_slot)
{
	player_input->add_device_slot(device_slot);
}

void LocalPlayer::remove_device_slot(int device_slot)
{
	player_input->remove_device_slot(device_slot);
}

bool LocalPlayer::has_device_slot(int device_slot) const
{
	return player_input->has_device_slot(device_slot);
}

void LocalPlayer::adopt_camera(Node* camera)
{
	if (camera == nullptr)
	{
		WARN_PRINT("GFGD: LocalPlayer::adopt_camera was given no camera.");
		return;
	}

	if (viewport == nullptr)
	{
		WARN_PRINT("GFGD: LocalPlayer::adopt_camera needs viewport_override to be set to the SubViewport this player renders into.");
		return;
	}

	if (!camera->is_inside_tree())
	{
		WARN_PRINT("GFGD: LocalPlayer::adopt_camera needs the camera to be inside the scene tree.");
		return;
	}

	if (camera->get_parent() != viewport)
	{
		camera->reparent(viewport, true);
	}

	if (Camera3D* camera_3d = Object::cast_to<Camera3D>(camera))
	{
		camera_3d->make_current();
	}
	else if (Camera2D* camera_2d = Object::cast_to<Camera2D>(camera))
	{
		camera_2d->make_current();
	}
}

void LocalPlayer::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_player_index"), &LocalPlayer::get_player_index);
	ClassDB::bind_method(D_METHOD("set_player_index", "value"), &LocalPlayer::set_player_index);
	ClassDB::bind_method(D_METHOD("get_player_input"), &LocalPlayer::get_player_input);

	ClassDB::bind_method(D_METHOD("get_device_slots"), &LocalPlayer::get_device_slots);
	ClassDB::bind_method(D_METHOD("set_device_slots", "value"), &LocalPlayer::set_device_slots);
	ClassDB::bind_method(D_METHOD("add_device_slot", "device_slot"), &LocalPlayer::add_device_slot);
	ClassDB::bind_method(D_METHOD("remove_device_slot", "device_slot"), &LocalPlayer::remove_device_slot);
	ClassDB::bind_method(D_METHOD("has_device_slot", "device_slot"), &LocalPlayer::has_device_slot);

	ClassDB::bind_method(D_METHOD("get_viewport_override"), &LocalPlayer::get_viewport_override);
	ClassDB::bind_method(D_METHOD("set_viewport_override", "value"), &LocalPlayer::set_viewport_override);
	ClassDB::bind_method(D_METHOD("adopt_camera", "camera"), &LocalPlayer::adopt_camera);

	ClassDB::bind_method(D_METHOD("get_player_controller"), &LocalPlayer::get_player_controller);
	ClassDB::bind_method(D_METHOD("set_player_controller", "value"), &LocalPlayer::set_player_controller);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "player_index"), "set_player_index", "get_player_index");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "device_slots"), "set_device_slots", "get_device_slots");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "viewport_override", PROPERTY_HINT_NODE_TYPE, "Viewport"), "set_viewport_override", "get_viewport_override");
}
}
