#ifndef PLAYER_CONTROLLER_H
#define PLAYER_CONTROLLER_H

#include <godot_cpp/classes/viewport.hpp>

#include "framework/controller.h"

using namespace godot;

namespace GFGD
{
class InputComponent;
class LocalPlayer;
class PlayerInput;
class PlayerState;

class PlayerController : public Controller
{
	GDCLASS(PlayerController, Controller)

private:
	InputComponent* input_component;
	Array current_input_stack;
	Array pending_remove_input_component;
	bool scope_lock;

	// The human this controller belongs to. Owned by the GameInstance, so it
	// outlives this controller across a level change. Null for a controller that
	// nobody logged in - which then reads the global Input, as before.
	LocalPlayer* local_player;

	// Lives under the GameState, not here.
	PlayerState* player_state;

public:
	PlayerController();
	~PlayerController();

	virtual void _enter_tree() override;
	virtual void _process(double delta) override;

	void set_pawn_camera_node_as_current();

	InputComponent* get_input_component() const { return input_component; }

	void register_input_component(InputComponent* input);
	void unregister_input_component(InputComponent* input);

	LocalPlayer* get_local_player() const { return local_player; }
	void set_local_player(LocalPlayer* value);

	PlayerState* get_player_state() const { return player_state; }
	void set_player_state(PlayerState* value) { player_state = value; }

	// Null while no LocalPlayer is assigned; every caller then falls back to the
	// global Input singleton.
	PlayerInput* get_player_input() const;

	// The LocalPlayer's split-screen viewport if it has one, else this node's.
	Viewport* get_player_viewport() const;

	int get_player_index() const;

	// Always true today. The hook exists so code written now stays correct once
	// a client only owns a subset of the world's player controllers.
	bool is_local_player_controller() const { return local_player != nullptr; }

protected:
	static void _bind_methods();

	virtual void on_possess(PawnHandler* pawn_handler) override;
	virtual void on_unpossess() override;
};
}

#endif
