#include "framework/pawn_handler.h"
#include <godot_cpp/core/class_db.hpp>

#include "framework/controller.h"
#include "framework/input_component.h"

using namespace godot;

namespace GFGD
{
PawnHandler::PawnHandler()
{
	controller = nullptr;
}

PawnHandler::~PawnHandler()
{

}

void PawnHandler::possessed_by(Controller* new_controller)
{
	controller = new_controller;
	GDVIRTUAL_CALL(_possessed, new_controller);
	emit_signal("possessed", new_controller);
}

void PawnHandler::unpossessed()
{
	controller = nullptr;
	GDVIRTUAL_CALL(_unpossessed);
	emit_signal("unpossessed");
}

void PawnHandler::setup_input_component(InputComponent* input_component)
{
	GDVIRTUAL_CALL(_setup_input_component, input_component);
}

void PawnHandler::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_controller"), &PawnHandler::get_controller);
	ClassDB::bind_method(D_METHOD("get_pawn_root"), &PawnHandler::get_pawn_root);
	ClassDB::bind_method(D_METHOD("setup_input_component", "input_component"), &PawnHandler::setup_input_component);

	ClassDB::bind_method(D_METHOD("get_camera_path"), &PawnHandler::get_camera_path);
	ClassDB::bind_method(D_METHOD("set_camera_path", "value"), &PawnHandler::set_camera_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "camera_path"), "set_camera_path", "get_camera_path");

	GDVIRTUAL_BIND(_possessed, "controller");
	GDVIRTUAL_BIND(_unpossessed);
	GDVIRTUAL_BIND(_setup_input_component, "input_component");

	ADD_SIGNAL(MethodInfo("possessed", PropertyInfo(Variant::OBJECT, "controller", PROPERTY_HINT_NODE_TYPE, "Controller")));
	ADD_SIGNAL(MethodInfo("unpossessed"));
}
}
