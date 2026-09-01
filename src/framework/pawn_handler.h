#ifndef PAWN_HANDLER_H
#define PAWN_HANDLER_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>

#include "framework/controller.h"
#include "framework/input_component.h"

using namespace godot;

namespace GFGD
{
// Attach as a direct child of the pawn's root node to make it possessable.
class PawnHandler : public Node
{
	GDCLASS(PawnHandler, Node)

private:
	Controller* controller;
	NodePath camera_path;

public:
	PawnHandler();
	~PawnHandler();

	void possessed_by(Controller* new_controller);
	void unpossessed();
	void setup_input_component(InputComponent* input_component);

	Controller* get_controller() const { return controller; }
	Node* get_pawn_root() const { return get_parent(); }

	NodePath get_camera_path() const { return camera_path; }
	void set_camera_path(const NodePath& value) { camera_path = value; }

	GDVIRTUAL1(_possessed, Controller*)
	GDVIRTUAL0(_unpossessed)
	GDVIRTUAL1(_setup_input_component, InputComponent*)

protected:
	static void _bind_methods();
};
}

#endif
