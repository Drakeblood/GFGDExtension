#ifndef GFGD_DEBUG_CAMERA_H
#define GFGD_DEBUG_CAMERA_H

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event.hpp>

using namespace godot;

namespace GFGD
{
// A free-flying camera for looking at the game from anywhere, on this machine
// only. The gameplay debugger turns it on and off (the free camera action, Tab
// by default, or "gd.freecam"); while it is on the player's pawn gets no input
// and stands where it was.
//
//   W A S D   fly          Q / E   down / up      mouse   look
//   Shift     faster       Ctrl    slower         wheel   change speed
//
// The keys are physical, so they sit in the same place on any layout. It moves
// in real time, so it still flies with the game slowed down ("slomo").
class DebugCamera : public Camera3D
{
	GDCLASS(DebugCamera, Camera3D)

private:
	float speed;
	float look_sensitivity;
	float yaw;
	float pitch;

public:
	DebugCamera();

	virtual void _ready() override;
	virtual void _process(double delta) override;
	virtual void _input(const Ref<InputEvent>& event) override;

	// Starts where another camera is, looking the same way.
	void start_from(const Transform3D& transform, float field_of_view);

	void set_speed(float value) { speed = value; }
	float get_speed() const { return speed; }
	void set_look_sensitivity(float value) { look_sensitivity = value; }
	float get_look_sensitivity() const { return look_sensitivity; }

protected:
	static void _bind_methods();

private:
	bool accepts_input() const;
	void apply_rotation();
};
}

#endif
