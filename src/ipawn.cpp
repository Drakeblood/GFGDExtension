#include "ipawn.h"
#include <godot_cpp/core/class_db.hpp>

#include "controller.h"
#include "input_component.h"

using namespace godot;

namespace GFGD
{
IPawn::IPawn()
{
	
}

IPawn::~IPawn()
{
	
}

void IPawn::possessed_by(Controller* new_controller)
{
	
}

void IPawn::unpossessed()
{
	
}

void IPawn::setup_input_component(InputComponent* input_component)
{
	
}

void IPawn::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("possessed_by", "new_controller"), &IPawn::possessed_by);
	ClassDB::bind_method(D_METHOD("unpossessed"), &IPawn::unpossessed);
	ClassDB::bind_method(D_METHOD("setup_input_component", "input_component"), &IPawn::setup_input_component);
}
}