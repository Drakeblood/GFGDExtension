#include "framework/controller.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "framework/gfgd_scene_tree.h"
#include "framework/pawn_handler.h"

using namespace godot;

namespace GFGD
{
Controller::Controller()
{
	pawn_handler = nullptr;
}

Controller::~Controller()
{

}

GFGDSceneTree* Controller::get_gfgd_scene_tree() const
{
	if (!is_inside_tree()) { return nullptr; }

	return Object::cast_to<GFGDSceneTree>(get_tree());
}

void Controller::_enter_tree()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	// Null when the project did not set run/main_loop_type to GFGDSceneTree. The
	// controller still works as a plain node; it just is not in any world list.
	if (GFGDSceneTree* scene_tree = get_gfgd_scene_tree())
	{
		scene_tree->register_controller(this);
	}
}

void Controller::_exit_tree()
{
	if (Engine::get_singleton()->is_editor_hint()) { return; }

	if (GFGDSceneTree* scene_tree = get_gfgd_scene_tree())
	{
		scene_tree->unregister_controller(this);
	}
}

void Controller::possess(PawnHandler* in_pawn_handler)
{
	if (in_pawn_handler == pawn_handler) { return; }

	if (pawn_handler != nullptr)
	{
		unpossess();
	}
	if (in_pawn_handler == nullptr) { return; }

	if (in_pawn_handler->get_controller() != nullptr)
	{
		in_pawn_handler->get_controller()->unpossess();
	}

	pawn_handler = in_pawn_handler;
	in_pawn_handler->possessed_by(this);

	on_possess(in_pawn_handler);
	GDVIRTUAL_CALL(_on_possess, in_pawn_handler);
	emit_signal("pawn_changed", in_pawn_handler);
}

void Controller::unpossess()
{
	if (pawn_handler == nullptr) { return; }

	PawnHandler* old_pawn_handler = pawn_handler;

	on_unpossess();
	GDVIRTUAL_CALL(_on_unpossess);

	pawn_handler = nullptr;
	old_pawn_handler->unpossessed();

	emit_signal("pawn_changed", (Object*)nullptr);
}

void Controller::on_possess(PawnHandler* in_pawn_handler)
{

}

void Controller::on_unpossess()
{

}

void Controller::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("possess", "pawn_handler"), &Controller::possess);
	ClassDB::bind_method(D_METHOD("unpossess"), &Controller::unpossess);
	ClassDB::bind_method(D_METHOD("get_pawn_handler"), &Controller::get_pawn_handler);
	ClassDB::bind_method(D_METHOD("get_gfgd_scene_tree"), &Controller::get_gfgd_scene_tree);

	GDVIRTUAL_BIND(_on_possess, "pawn_handler");
	GDVIRTUAL_BIND(_on_unpossess);

	ADD_SIGNAL(MethodInfo("pawn_changed", PropertyInfo(Variant::OBJECT, "pawn_handler", PROPERTY_HINT_NODE_TYPE, "PawnHandler")));
}
}
