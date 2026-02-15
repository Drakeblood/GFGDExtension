#include "controller.h"
#include <godot_cpp/core/class_db.hpp>

#include "pawn_handler.h"

using namespace godot;

namespace GFGD
{
Controller::Controller()
{
	
}

Controller::~Controller()
{
	
}

void Controller::set_pawn_handler(PawnHandler* in_pawn_handler)
{
	pawn_handler = in_pawn_handler;
}

void Controller::possess(PawnHandler* in_pawn_handler)
{
	PawnHandler* current_pawn = pawn_handler;

	on_possess(in_pawn_handler);
}

void Controller::unpossess()
{
	PawnHandler* current_pawn = pawn_handler;
	if (current_pawn == nullptr) { return; }

	on_unpossess();
}

void Controller::on_possess(PawnHandler* in_pawn_handler)
{
	bool b_new_pawn_handler = pawn_handler != in_pawn_handler;

	if (b_new_pawn_handler && pawn_handler != nullptr) { unpossess(); }
	if (in_pawn_handler == nullptr) { return; }

	if (in_pawn_handler->get_controller() != nullptr)
	{
		in_pawn_handler->get_controller()->unpossess();
	}

	in_pawn_handler->possessed_by(this);
	set_pawn_handler(in_pawn_handler);
}

void Controller::on_unpossess()
{
	if (pawn_handler != nullptr)
	{
		pawn_handler->unpossessed();
	}

	set_pawn_handler(nullptr);
}

void Controller::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_pawn_handler", "pawn_handler"), &Controller::set_pawn_handler);
	ClassDB::bind_method(D_METHOD("get_pawn_handler"), &Controller::get_pawn_handler);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "pawn_handler", PROPERTY_HINT_RESOURCE_TYPE, "PawnHandler"), "set_pawn_handler", "get_pawn_handler");

	ClassDB::bind_method(D_METHOD("possess", "pawn_handler"), &Controller::possess);
	ClassDB::bind_method(D_METHOD("unpossess"), &Controller::unpossess);
}
}