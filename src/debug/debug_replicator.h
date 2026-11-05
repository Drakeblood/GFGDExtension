#ifndef GFGD_DEBUG_REPLICATOR_H
#define GFGD_DEBUG_REPLICATOR_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

using namespace godot;

namespace GFGD
{
class PlayerController;

// The debug tools' wire: the gameplay debugger's data and the console's
// server commands. One sits under every PlayerController (named
// "DebugReplicator"), on the server and on the owning client, so its remote
// calls have the same path at both ends and only that client can use them.
//
//  - Console: a client's FLAG_SERVER commands and cheats travel up as a line of
//    text and run on the server as that player; their output travels back.
//  - Gameplay debugger: the client says what it is looking at and which
//    categories are on; the server collects those categories for that node
//    every refresh_interval and sends the result back to that client only.
//
// On the server's own player (listen server, single player) nothing goes over
// the network - the same calls are made directly.
class DebugReplicator : public Node
{
	GDCLASS(DebugReplicator, Node)

private:
	// Server side: what the owning player asked to see.
	bool view_active;
	NodePath view_target;
	PackedStringArray view_categories;
	double collect_accumulator;

	// Client side: the last thing the server sent.
	Dictionary view_data;
	NodePath view_data_target;
	uint64_t view_data_msec;

public:
	DebugReplicator();

	static DebugReplicator* find_for(PlayerController* controller);

	virtual void _ready() override;
	virtual void _process(double delta) override;

	PlayerController* get_player_controller() const;

	// --- Console ---------------------------------------------------------------
	void request_exec(const String& line);
	void send_output(const String& text, int level);
	void server_exec(const String& line);
	void client_output(const String& text, int level);

	// --- Gameplay debugger -----------------------------------------------------
	void request_view(bool active, const NodePath& target, const PackedStringArray& categories);
	void server_set_view(bool active, const NodePath& target, const PackedStringArray& categories);
	void client_receive_view(const NodePath& target, const Dictionary& data);

	Dictionary get_view_data() const { return view_data; }
	NodePath get_view_data_target() const { return view_data_target; }
	// Seconds since the last data arrived; -1 before any did.
	double get_view_data_age() const;

protected:
	static void _bind_methods();

private:
	bool is_server() const;
	bool is_from_owner() const;
	bool is_from_server() const;
	bool is_local_owner() const;
	void collect_and_send();
	void apply_view(bool active, const NodePath& target, const PackedStringArray& categories);
};
}

#endif
