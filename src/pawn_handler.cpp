#include "pawn_handler.h"
#include <godot_cpp/core/class_db.hpp>

#include "controller.h"
#include "input_component.h"

using namespace godot;

namespace GFGD
{
PawnHandler::PawnHandler()
{
	
}

PawnHandler::~PawnHandler()
{
	
}

void PawnHandler::possessed_by(Controller* new_controller)
{
	controller = new_controller;
}

void PawnHandler::unpossessed()
{
	controller = nullptr;
}

void PawnHandler::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_controller"), &PawnHandler::get_controller);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "controller", PROPERTY_HINT_RESOURCE_TYPE, "Controller"), "", "get_controller");
}
}