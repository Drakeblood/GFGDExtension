#include "framework/ai_controller.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "framework/pawn.h"
#include "framework/world.h"

using namespace godot;

namespace GFGD
{
AIController::AIController()
{

}

AIController::~AIController()
{

}

void AIController::_enter_tree()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	// godot-cpp only binds the most derived override, so the base call is what
	// keeps this controller in the world's lists.
	Controller::_enter_tree();
}

bool AIController::possess_pawn_root(Node* pawn_root)
{
	Pawn* pawn = Pawn::find_in(pawn_root);
	if (pawn == nullptr)
	{
		WARN_PRINT("GFGD: that node has no Pawn, so it cannot be possessed.");
		return false;
	}

	possess(pawn);
	return true;
}

void AIController::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("possess_pawn_root", "pawn_root"), &AIController::possess_pawn_root);
}
}
