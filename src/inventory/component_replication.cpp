#include "inventory/component_replication.h"

#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "framework/net_driver.h"
#include "framework/world.h"

namespace GFGD
{
namespace ComponentReplication
{
World* world_of(const Node* node)
{
	return node != nullptr && node->is_inside_tree() ? Object::cast_to<World>(node->get_tree()) : nullptr;
}

bool is_mirror(const Node* node, Audience audience)
{
	if (audience == AUDIENCE_NONE) { return false; }
	World* world = world_of(node);
	return world != nullptr && world->is_networked() && !world->has_authority();
}

bool is_sending_server(const Node* node, Audience audience)
{
	if (audience == AUDIENCE_NONE) { return false; }
	World* world = world_of(node);
	return world != nullptr && world->is_networked() && world->has_authority();
}

int get_owner_peer(const Node* node)
{
	World* world = world_of(node);
	if (world == nullptr) { return World::SERVER_PEER_ID; }
	Node* parent = node->get_parent();
	return world->get_net_owner_peer(parent != nullptr ? parent : const_cast<Node*>(node));
}

PackedInt32Array resolve_peers(const Node* node, Audience audience, HashSet<int>& subscribers)
{
	PackedInt32Array peers;
	if (audience == AUDIENCE_NONE || subscribers.is_empty()) { return peers; }

	Ref<MultiplayerAPI> multiplayer = node->get_multiplayer();
	const PackedInt32Array connected = multiplayer.is_valid() ? multiplayer->get_peers() : PackedInt32Array();
	const int owner = audience == AUDIENCE_OWNER ? get_owner_peer(node) : 0;
	PackedInt32Array gone;
	for (const int peer : subscribers)
	{
		if (!connected.has(peer))
		{
			gone.push_back(peer);
			continue;
		}
		if (audience == AUDIENCE_OWNER && peer != owner) { continue; }
		peers.push_back(peer);
	}
	for (int i = 0; i < gone.size(); i++) { subscribers.erase(gone[i]); }
	return peers;
}

bool sender_is_owner(const Node* node, const char* what)
{
	Ref<MultiplayerAPI> multiplayer = node->get_multiplayer();
	const int sender = multiplayer.is_valid() ? multiplayer->get_remote_sender_id() : 0;
	const int owner = get_owner_peer(node);
	if (sender != owner)
	{
		WARN_PRINT(vformat("GFGD: peer %d sent %s to %s, which peer %d owns; ignoring it.", sender, String(what), String(node->get_path()), owner));
		return false;
	}
	return true;
}

void configure_rpcs(Node* node, const PackedStringArray& from_anyone, const PackedStringArray& from_server)
{
	Dictionary anyone;
	anyone["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
	anyone["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
	anyone["call_local"] = false;
	anyone["channel"] = 0;

	Dictionary server = anyone.duplicate();
	server["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;

	for (int i = 0; i < from_anyone.size(); i++) { node->rpc_config(from_anyone[i], anyone); }
	for (int i = 0; i < from_server.size(); i++) { node->rpc_config(from_server[i], server); }
}

String loadable_path(const Ref<Resource>& resource)
{
	if (resource.is_null()) { return String(); }
	const String path = resource->get_path();
	if (path.is_empty() || path.contains("::")) { return String(); }
	return path;
}
}
}
