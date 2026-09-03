#ifndef PLAYER_CONTROLLER_H
#define PLAYER_CONTROLLER_H

#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "framework/controller.h"

using namespace godot;

namespace GFGD
{
class InputComponent;
class LocalPlayer;
class PlayerInput;

// A human's controller. It exists on the server for every player and on each
// client for that client's own player, and nowhere else - which is what makes it
// the right place to send a player's input from and the wrong place to keep
// anything everyone has to see.
class PlayerController : public Controller
{
	GDCLASS(PlayerController, Controller)

private:
	InputComponent* input_component;
	Array current_input_stack;
	Array pending_remove_input_component;
	bool scope_lock;

	// The human this controller belongs to. Owned by the GameInstance, so it
	// outlives this controller across a level change. Null on the server for a
	// player sitting at another machine, and on a dedicated server for everyone.
	LocalPlayer* local_player;

	// Where a remote player's input is written when it arrives, so that from the
	// input component down the server runs exactly the code a local player runs.
	PlayerInput* remote_player_input;

	// The connection this controller belongs to. Server calls that arrive from
	// anyone else are dropped.
	int owner_peer_id;

	// Empty means every action in the input map that is not a built-in ui_ one.
	PackedStringArray replicated_actions;
	PackedStringArray replicated_action_cache;

	// Last state sent to the server, so only what changed goes over the wire.
	Dictionary sent_action_state;

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

	// The action state this controller reads. A local player's own state, or the
	// state the server was sent for a player at another machine.
	PlayerInput* get_player_input() const;

	// The LocalPlayer's split-screen viewport if it has one, else this node's.
	Viewport* get_player_viewport() const;

	int get_player_index() const;

	virtual int get_owner_peer_id() const override { return owner_peer_id; }
	void set_owner_peer_id(int value) { owner_peer_id = value; }

	virtual bool is_player_controller() const override { return true; }
	virtual bool is_local_controller() const override;

	// True where this player is actually sitting: it has a controller of its own
	// and a human behind it.
	bool is_local_player_controller() const;

	PackedStringArray get_replicated_actions() const { return replicated_actions; }
	void set_replicated_actions(const PackedStringArray& value);

	// Remote call: the owning client's action state, applied to this controller's
	// PlayerInput on the server.
	void server_receive_input(const PackedStringArray& action_names, const PackedByteArray& pressed, const PackedFloat32Array& strengths);

protected:
	static void _bind_methods();

	virtual void on_possess(Pawn* pawn) override;
	virtual void on_unpossess() override;

private:
	void configure_rpcs();
	void pump_input(double delta);
	void send_input_to_server();
	const PackedStringArray& get_replicated_action_list();
};
}

#endif
