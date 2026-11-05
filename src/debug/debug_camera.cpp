#include "debug/debug_camera.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "debug/console_manager.h"

using namespace GFGD;

namespace
{
constexpr float MIN_SPEED = 0.5f;
constexpr float MAX_SPEED = 200.0f;
constexpr float MAX_PITCH = 1.55f;
}

DebugCamera::DebugCamera()
{
	speed = 8.0f;
	look_sensitivity = 0.003f;
	yaw = 0.0f;
	pitch = 0.0f;
}

void DebugCamera::_ready()
{
	// Keeps flying while the tree is paused.
	set_process_mode(PROCESS_MODE_ALWAYS);
	set_process(true);
	set_process_input(true);
}

void DebugCamera::start_from(const Transform3D& transform, float field_of_view)
{
	set_global_transform(transform);
	set_fov(field_of_view);
	const Vector3 forward = -transform.basis.get_column(2).normalized();
	yaw = Math::atan2(-forward.x, -forward.z);
	pitch = Math::asin(CLAMP(forward.y, -1.0f, 1.0f));
	apply_rotation();
}

bool DebugCamera::accepts_input() const
{
	ConsoleManager* console = ConsoleManager::get_singleton();
	return is_current() && (console == nullptr || !console->is_console_open());
}

void DebugCamera::apply_rotation()
{
	Basis basis;
	basis.rotate(Vector3(1, 0, 0), pitch);
	basis.rotate(Vector3(0, 1, 0), yaw);
	Transform3D transform = get_global_transform();
	transform.basis = basis;
	set_global_transform(transform);
}

void DebugCamera::_input(const Ref<InputEvent>& event)
{
	if (!accepts_input()) { return; }

	Ref<InputEventMouseMotion> motion = event;
	if (motion.is_valid() && Input::get_singleton()->get_mouse_mode() == Input::MOUSE_MODE_CAPTURED)
	{
		const Vector2 relative = motion->get_screen_relative();
		yaw -= relative.x * look_sensitivity;
		pitch = CLAMP(pitch - relative.y * look_sensitivity, -MAX_PITCH, MAX_PITCH);
		apply_rotation();
		get_viewport()->set_input_as_handled();
		return;
	}

	Ref<InputEventMouseButton> button = event;
	if (button.is_valid() && button->is_pressed())
	{
		if (button->get_button_index() == MOUSE_BUTTON_WHEEL_UP) { speed = MIN(speed * 1.25f, MAX_SPEED); }
		else if (button->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN) { speed = MAX(speed / 1.25f, MIN_SPEED); }
	}
}

void DebugCamera::_process(double delta)
{
	if (!accepts_input()) { return; }

	// Real time: the engine scales delta by the time scale, and this camera
	// should not crawl because the game does.
	const double time_scale = Engine::get_singleton()->get_time_scale();
	const double real_delta = time_scale > 0.0 ? delta / time_scale : delta;

	Input* input = Input::get_singleton();
	Vector3 direction;
	if (input->is_physical_key_pressed(KEY_W)) { direction.z -= 1.0f; }
	if (input->is_physical_key_pressed(KEY_S)) { direction.z += 1.0f; }
	if (input->is_physical_key_pressed(KEY_A)) { direction.x -= 1.0f; }
	if (input->is_physical_key_pressed(KEY_D)) { direction.x += 1.0f; }
	if (direction.length_squared() == 0.0f && !input->is_physical_key_pressed(KEY_Q) && !input->is_physical_key_pressed(KEY_E)) { return; }

	float current_speed = speed;
	if (input->is_physical_key_pressed(KEY_SHIFT)) { current_speed *= 4.0f; }
	if (input->is_physical_key_pressed(KEY_CTRL)) { current_speed *= 0.25f; }

	// W/S follow the view, so looking down and pressing W dives; Q/E are world up and down.
	Vector3 motion = get_global_transform().basis.xform(direction);
	if (input->is_physical_key_pressed(KEY_Q)) { motion.y -= 1.0f; }
	if (input->is_physical_key_pressed(KEY_E)) { motion.y += 1.0f; }
	if (motion.length_squared() > 1.0f) { motion = motion.normalized(); }

	set_global_position(get_global_position() + motion * current_speed * real_delta);
}

void DebugCamera::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("start_from", "transform", "field_of_view"), &DebugCamera::start_from);
	ClassDB::bind_method(D_METHOD("set_speed", "value"), &DebugCamera::set_speed);
	ClassDB::bind_method(D_METHOD("get_speed"), &DebugCamera::get_speed);
	ClassDB::bind_method(D_METHOD("set_look_sensitivity", "value"), &DebugCamera::set_look_sensitivity);
	ClassDB::bind_method(D_METHOD("get_look_sensitivity"), &DebugCamera::get_look_sensitivity);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed", PROPERTY_HINT_RANGE, "0.5,200,0.1,suffix:m/s"), "set_speed", "get_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "look_sensitivity"), "set_look_sensitivity", "get_look_sensitivity");
}
