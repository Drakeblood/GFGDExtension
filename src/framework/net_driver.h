#ifndef NET_DRIVER_H
#define NET_DRIVER_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

using namespace godot;

namespace GFGD
{
class PlayerController;
class PlayerState;
class World;

// Connections, travel and the mirroring of everything the server creates.
//
// It sits at /root/NetDriver on every peer and is never freed with a level,
// which is what lets it be the address remote calls are sent to: a call only
// arrives if the node holding it is at the same path on both sides.
//
// Spawning goes through here rather than through a MultiplayerSpawner because a
// client is only sent what it can receive: a peer counts as ready once it has
// reported the current level up, and until then nothing is sent to it. That is
// also what makes a late join and a level change the same case - the peer is
// handed the whole world once, in one place.
class NetDriver : public Node
{
	GDCLASS(NetDriver, Node)

private:
	World* world;

	// Peers with the current level built. Cleared whenever the level changes.
	PackedInt32Array ready_peers;

	// player_id -> the arguments the peer needs to build the same node. Kept so a
	// peer that becomes ready later can be handed everything at once.
	Dictionary player_state_records;
	Dictionary pawn_records;

public:
	NetDriver();
	~NetDriver();

	virtual void _ready() override;

	void set_world(World* value) { world = value; }
	World* get_world() const { return world; }

	// --- Server side --------------------------------------------------------

	// Tells every client to load resource_path and stops treating them as ready.
	void begin_travel(const String& level_path);

	bool is_peer_ready(int peer_id) const;
	PackedInt32Array get_ready_peers() const { return ready_peers; }

	// Forgets what the current level had. Called when the level is torn down.
	void clear_records();

	PlayerState* spawn_player_state(int player_id, int owner_peer, const String& player_name, int player_index, bool bot);
	void despawn_player_state(int player_id);

	PlayerController* spawn_player_controller(int player_id, int owner_peer);
	void despawn_player_controller(int player_id);

	Node* spawn_pawn(int player_id, const Ref<PackedScene>& pawn_scene, const Variant& spawn_transform);
	void despawn_pawn(int player_id);

	// --- Remote calls -------------------------------------------------------

	void client_travel_to_level(const String& level_path);
	void server_notify_level_loaded();
	void client_spawn_player_state(const Dictionary& data);
	void client_despawn_player_state(int player_id);
	void client_spawn_pawn(const Dictionary& data);
	void client_despawn_pawn(int player_id);
	void client_create_player_controller(const Dictionary& data);
	void client_destroy_player_controller(int player_id);

protected:
	static void _bind_methods();

private:
	void configure_rpcs();

	void on_peer_connected(int peer_id);
	void on_peer_disconnected(int peer_id);
	void on_connected_to_server();
	void on_connection_failed();
	void on_server_disconnected();

	// Everything the peer missed, in the order it has to arrive in: player states
	// first, then pawns, then the peer's own controller.
	void send_world_state_to(int peer_id);

	void send_to_ready_peers(const StringName& method, const Variant& argument);

	// scene_hint is the scene the server already holds; a peer applying a remote
	// call passes nothing and loads the path out of the data instead.
	PlayerState* build_player_state(const Dictionary& data, const Ref<PackedScene>& scene_hint);
	PlayerController* build_player_controller(const Dictionary& data, const Ref<PackedScene>& scene_hint);
	Node* build_pawn(const Dictionary& data, const Ref<PackedScene>& scene_hint);

	Node* find_pawn_root(int player_id) const;
	String scene_path_or_warn(const Ref<PackedScene>& scene, const char* what) const;
};
}

#endif
