#include "debug/gameplay_debugger_category.h"

#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/e_net_multiplayer_peer.hpp>
#include <godot_cpp/classes/e_net_packet_peer.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/performance.hpp>
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "ability_system/ability_system_component.h"
#include "ability_system/ability_system_debugger.h"
#include "debug/debug_draw.h"
#include "framework/controller.h"
#include "framework/pawn.h"
#include "framework/player_controller.h"
#include "framework/player_state.h"
#include "framework/world.h"
#include "movement/character_movement_component.h"
#include "movement/movement_types.h"

using namespace GFGD;

namespace
{
const char* COLOR_DIM = "#9aa4b1";
const char* COLOR_GOOD = "#8fe388";
const char* COLOR_BAD = "#ff8a7a";
const char* COLOR_MOD = "#7cc7ff";

String dim(const String& text) { return String("[color=") + COLOR_DIM + "]" + text + "[/color]"; }
String colored(const char* color, const String& text) { return String("[color=") + color + "]" + text + "[/color]"; }

String fmt_vec(const Vector3& v, int decimals = 2)
{
	return "(" + String::num(v.x, decimals) + ", " + String::num(v.y, decimals) + ", " + String::num(v.z, decimals) + ")";
}

String role_name(int role)
{
	switch (role)
	{
		case World::ROLE_SIMULATED_PROXY: return "simulated proxy";
		case World::ROLE_AUTONOMOUS_PROXY: return "autonomous proxy";
		case World::ROLE_AUTHORITY: return "authority";
	}
	return "none";
}

String script_label(Object* object)
{
	Ref<Script> script = object->get_script();
	if (script.is_null()) { return String(); }
	const StringName global = script->get_global_name();
	return global != StringName() ? String(global) : script->get_path().get_file();
}

Node3D* as_3d(Node* node)
{
	Node3D* node_3d = Object::cast_to<Node3D>(node);
	if (node_3d != nullptr) { return node_3d; }
	// A pawn's root may be a plain Node; its body is then the first 3D child.
	if (node == nullptr) { return nullptr; }
	TypedArray<Node> bodies = node->find_children("*", "Node3D", false, false);
	return bodies.is_empty() ? nullptr : Object::cast_to<Node3D>(bodies[0]);
}

Vector3 velocity_of(Node* node)
{
	if (CharacterMovementComponent* cmc = Object::cast_to<CharacterMovementComponent>(GameplayDebuggerCategory::find_component(node, "CharacterMovementComponent")))
	{
		return cmc->get_velocity();
	}
	if (RigidBody3D* rigid = Object::cast_to<RigidBody3D>(node)) { return rigid->get_linear_velocity(); }
	if (CharacterBody3D* character = Object::cast_to<CharacterBody3D>(node)) { return character->get_velocity(); }
	return Vector3();
}
}

GameplayDebuggerCategory::GameplayDebuggerCategory()
{
	enabled_by_default = true;
}

Dictionary GameplayDebuggerCategory::collect_data(Node* target, PlayerController* viewer)
{
	Dictionary result;
	GDVIRTUAL_CALL(_collect_data, target, viewer, result);
	return result;
}

String GameplayDebuggerCategory::draw_data(Node* target, const Dictionary& data)
{
	String result;
	GDVIRTUAL_CALL(_draw_data, target, data, result);
	return result;
}

void GameplayDebuggerCategory::draw_world(Node* target, const Dictionary& data)
{
	GDVIRTUAL_CALL(_draw_world, target, data);
}

Node* GameplayDebuggerCategory::find_component(Node* target, const String& class_name)
{
	if (target == nullptr) { return nullptr; }
	if (target->is_class(class_name)) { return target; }
	// A deep search from the root window would walk the whole tree every frame.
	if (Object::cast_to<Viewport>(target) != nullptr) { return nullptr; }

	Node* root = target;
	if (Pawn* pawn = Object::cast_to<Pawn>(target)) { root = pawn->get_pawn_root(); }
	if (root == nullptr) { return nullptr; }

	TypedArray<Node> found = root->find_children("*", class_name, true, false);
	return found.is_empty() ? nullptr : Object::cast_to<Node>(found[0]);
}

void GameplayDebuggerCategory::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_category_name"), &GameplayDebuggerCategory::get_category_name);
	ClassDB::bind_method(D_METHOD("set_category_name", "value"), &GameplayDebuggerCategory::set_category_name);
	ClassDB::bind_method(D_METHOD("is_enabled_by_default"), &GameplayDebuggerCategory::is_enabled_by_default);
	ClassDB::bind_method(D_METHOD("set_enabled_by_default", "value"), &GameplayDebuggerCategory::set_enabled_by_default);
	ClassDB::bind_static_method("GameplayDebuggerCategory", D_METHOD("find_component", "target", "class_name"), &GameplayDebuggerCategory::find_component);
	// The callers' side of the virtuals below, so a script can run any category
	// (the built-in ones included) by hand.
	ClassDB::bind_method(D_METHOD("collect_data", "target", "viewer"), &GameplayDebuggerCategory::collect_data);
	ClassDB::bind_method(D_METHOD("draw_data", "target", "data"), &GameplayDebuggerCategory::draw_data);
	ClassDB::bind_method(D_METHOD("draw_world", "target", "data"), &GameplayDebuggerCategory::draw_world);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "category_name"), "set_category_name", "get_category_name");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled_by_default"), "set_enabled_by_default", "is_enabled_by_default");

	GDVIRTUAL_BIND(_collect_data, "target", "viewer");
	GDVIRTUAL_BIND(_draw_data, "target", "data");
	GDVIRTUAL_BIND(_draw_world, "target", "data");
}

// --- Basic --------------------------------------------------------------------

GameplayDebuggerCategoryBasic::GameplayDebuggerCategoryBasic()
{
	set_category_name("Basic");
}

Dictionary GameplayDebuggerCategoryBasic::collect_data(Node* target, PlayerController* /*viewer*/)
{
	Dictionary data;
	if (target == nullptr) { return data; }

	World* world = Object::cast_to<World>(target->get_tree());
	data["name"] = String(target->get_name());
	data["class"] = target->get_class();
	data["script"] = script_label(target);
	data["authority"] = target->get_multiplayer_authority();
	if (world != nullptr) { data["owner_peer"] = world->get_net_owner_peer(target); }

	if (Node3D* node = as_3d(target))
	{
		data["position"] = node->get_global_position();
		data["rotation"] = node->get_global_rotation_degrees();
		data["velocity"] = velocity_of(node == target ? target : node);
	}

	if (Pawn* pawn = Pawn::find_in(target))
	{
		Controller* controller = pawn->get_controller();
		data["controller"] = controller != nullptr ? String(controller->get_name()) : String("-");
		PlayerState* state = pawn->get_player_state();
		data["player_state"] = state != nullptr ? String(state->get_name()) : String("-");
	}

	PackedStringArray groups;
	const TypedArray<StringName> node_groups = target->get_groups();
	for (int i = 0; i < node_groups.size(); i++)
	{
		const String group = node_groups[i];
		if (!group.begins_with("_")) { groups.push_back(group); }
	}
	data["groups"] = groups;
	return data;
}

String GameplayDebuggerCategoryBasic::draw_data(Node* target, const Dictionary& data)
{
	if (data.is_empty()) { return dim("No target."); }

	String out = "[b]" + String(data.get("name", "?")) + "[/b]  " + String(data.get("class", ""));
	const String script = data.get("script", String());
	if (!script.is_empty()) { out += "  " + dim(script); }
	out += "\n";

	if (data.has("position"))
	{
		const Vector3 velocity = data.get("velocity", Vector3());
		out += "pos " + fmt_vec(data["position"]) + "   rot " + fmt_vec(data["rotation"], 0) + "\n";
		out += "vel " + fmt_vec(velocity) + "   " + String::num(velocity.length(), 2) + " m/s\n";
	}

	String net = "authority peer " + String::num_int64(data.get("authority", 1)) + "   owner peer " + String::num_int64(data.get("owner_peer", 1));
	if (target != nullptr)
	{
		World* world = Object::cast_to<World>(target->get_tree());
		if (world != nullptr) { net += "   here: " + role_name(world->get_local_role_for(target)); }
	}
	out += net + "\n";

	if (data.has("controller"))
	{
		out += "controller " + String(data["controller"]) + "   player state " + String(data["player_state"]) + "\n";
	}
	const PackedStringArray groups = data.get("groups", PackedStringArray());
	if (!groups.is_empty()) { out += dim("groups: " + String(", ").join(groups)) + "\n"; }
	return out;
}

void GameplayDebuggerCategoryBasic::draw_world(Node* target, const Dictionary& data)
{
	DebugDraw* draw = DebugDraw::get_singleton();
	Node3D* node = as_3d(target);
	if (draw == nullptr || node == nullptr) { return; }

	const Vector3 position = node->get_global_position();
	const Color marker(1.0, 0.83, 0.43);
	draw->line(position, position + Vector3(0, 2.4, 0), marker);
	draw->sphere(position + Vector3(0, 2.4, 0), 0.08, marker);
	draw->text(position + Vector3(0, 2.6, 0), String(target->get_name()), marker);

	const Vector3 velocity = velocity_of(target);
	if (velocity.length() > 0.05)
	{
		draw->arrow(position + Vector3(0, 0.1, 0), position + Vector3(0, 0.1, 0) + velocity * 0.5, Color(0.56, 0.89, 0.53));
	}
	if (data.has("position"))
	{
		// Where the server has it. A gap between this and the marker is lag,
		// interpolation, or a desync.
		const Vector3 server_position = data["position"];
		if (server_position.distance_to(position) > 0.05)
		{
			draw->sphere(server_position, 0.2, Color(1, 0.54, 0.48));
			draw->line(server_position, position, Color(1, 0.54, 0.48));
		}
	}
}

// --- Abilities ----------------------------------------------------------------

GameplayDebuggerCategoryAbilities::GameplayDebuggerCategoryAbilities()
{
	set_category_name("Abilities");
}

Dictionary GameplayDebuggerCategoryAbilities::collect_data(Node* target, PlayerController* /*viewer*/)
{
	Dictionary data;
	AbilitySystemComponent* asc = Object::cast_to<AbilitySystemComponent>(find_component(target, "AbilitySystemComponent"));
	if (asc == nullptr) { return data; }
	data["snapshot"] = asc->get_debug_snapshot();
	data["path"] = String(asc->get_path());
	return data;
}

String GameplayDebuggerCategoryAbilities::draw_data(Node* target, const Dictionary& data)
{
	if (!data.has("snapshot")) { return dim("No AbilitySystemComponent."); }

	const Dictionary server = data["snapshot"];
	String out = dim(String(data.get("path", "")) + "  (server)") + "\n";
	out += AbilitySystemDebugger::format_snapshot(server);

	// Against the local copy: on a client, tags and attributes the server has
	// but the client does not are the "I don't see it" bugs.
	AbilitySystemComponent* local = Object::cast_to<AbilitySystemComponent>(find_component(target, "AbilitySystemComponent"));
	World* world = target != nullptr ? Object::cast_to<World>(target->get_tree()) : nullptr;
	if (local == nullptr || world == nullptr || world->has_authority()) { return out; }

	const Dictionary mine = local->get_debug_snapshot();
	PackedStringArray differences;
	const Dictionary server_tags = server.get("tags", Dictionary());
	const Dictionary local_tags = mine.get("tags", Dictionary());
	const Array server_tag_names = server_tags.keys();
	for (int i = 0; i < server_tag_names.size(); i++)
	{
		if (!local_tags.has(server_tag_names[i])) { differences.push_back("missing tag " + String(server_tag_names[i])); }
	}
	const Array local_tag_names = local_tags.keys();
	for (int i = 0; i < local_tag_names.size(); i++)
	{
		if (!server_tags.has(local_tag_names[i])) { differences.push_back("extra tag " + String(local_tag_names[i])); }
	}
	const Dictionary server_attributes = server.get("attributes", Dictionary());
	const Dictionary local_attributes = mine.get("attributes", Dictionary());
	const Array attribute_names = server_attributes.keys();
	for (int i = 0; i < attribute_names.size(); i++)
	{
		const Array s = server_attributes[attribute_names[i]];
		const Array l = local_attributes.get(attribute_names[i], Array());
		if (l.size() < 2) { differences.push_back("missing attribute " + String(attribute_names[i])); continue; }
		if (!Math::is_equal_approx(double(s[1]), double(l[1])))
		{
			differences.push_back(String(attribute_names[i]) + " " + String::num(double(l[1]), 2) + " here vs " + String::num(double(s[1]), 2));
		}
	}

	out += differences.is_empty()
		? colored(COLOR_GOOD, "client mirror matches the server") + "\n"
		: colored(COLOR_BAD, "client mirror differs: " + String(", ").join(differences)) + "\n";
	return out;
}

// --- Movement -----------------------------------------------------------------

GameplayDebuggerCategoryMovement::GameplayDebuggerCategoryMovement()
{
	set_category_name("Movement");
}

Dictionary GameplayDebuggerCategoryMovement::collect_data(Node* target, PlayerController* /*viewer*/)
{
	Dictionary data;
	CharacterMovementComponent* cmc = Object::cast_to<CharacterMovementComponent>(find_component(target, "CharacterMovementComponent"));
	if (cmc == nullptr) { return data; }

	data["mode"] = cmc->get_movement_mode();
	data["velocity"] = cmc->get_velocity();
	data["on_ground"] = cmc->is_on_ground();
	data["replaying"] = cmc->is_replaying();
	if (PhysicsBody3D* body = cmc->get_updated_body()) { data["position"] = body->get_global_position(); }

	Ref<FloorResult> floor = cmc->get_current_floor();
	if (floor.is_valid())
	{
		data["floor_walkable"] = floor->is_walkable_floor();
		data["floor_hit"] = floor->get_blocking_hit();
		data["floor_distance"] = floor->get_distance_to_floor();
		data["floor_normal"] = floor->get_normal();
		data["floor_point"] = floor->get_point();
		Node* collider = Object::cast_to<Node>(floor->get_collider());
		data["floor_collider"] = collider != nullptr ? String(collider->get_name()) : String("-");
		data["floor_velocity"] = floor->get_collider_velocity();
	}

	Ref<MovementState> state = cmc->get_state();
	if (state.is_valid() && state->has_base())
	{
		Node* base = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(uint64_t(state->get_base_id()))));
		data["base"] = base != nullptr ? String(base->get_name()) : String("(gone)");
		data["base_relative"] = state->get_base_relative_position();
	}
	data["layered_moves"] = cmc->get_active_layered_moves().size();
	return data;
}

String GameplayDebuggerCategoryMovement::draw_data(Node* target, const Dictionary& data)
{
	if (data.is_empty()) { return dim("No CharacterMovementComponent."); }

	const Vector3 velocity = data.get("velocity", Vector3());
	const Vector3 horizontal(velocity.x, 0, velocity.z);
	String out = "mode [b]" + String(data.get("mode", "?")) + "[/b]   "
		+ (bool(data.get("on_ground", false)) ? colored(COLOR_GOOD, "on ground") : colored(COLOR_MOD, "in air"))
		+ "   speed " + String::num(horizontal.length(), 2) + " m/s  vz " + String::num(velocity.y, 2) + "\n";

	if (data.has("floor_hit"))
	{
		const bool hit = data["floor_hit"];
		out += "floor " + (hit ? String(data["floor_collider"]) : String("none"))
			+ (hit ? "  dist " + String::num(double(data["floor_distance"]), 3) + "  n " + fmt_vec(data["floor_normal"]) : String())
			+ (hit && !bool(data["floor_walkable"]) ? "  " + colored(COLOR_BAD, "not walkable") : String()) + "\n";
		const Vector3 floor_velocity = data.get("floor_velocity", Vector3());
		if (floor_velocity.length() > 0.01) { out += "floor moving " + fmt_vec(floor_velocity) + "\n"; }
	}
	out += data.has("base")
		? "base " + String(data["base"]) + "  rel " + fmt_vec(data["base_relative"]) + "\n"
		: dim("no base") + "\n";
	const int layered = data.get("layered_moves", 0);
	if (layered > 0) { out += "layered moves: " + String::num_int64(layered) + "\n"; }

	// The client's copy against the server's: on a predicted pawn this is the
	// prediction error, on a simulated one the interpolation lag.
	CharacterMovementComponent* local = Object::cast_to<CharacterMovementComponent>(find_component(target, "CharacterMovementComponent"));
	World* world = target != nullptr ? Object::cast_to<World>(target->get_tree()) : nullptr;
	if (local != nullptr && world != nullptr && !world->has_authority() && data.has("position") && local->get_updated_body() != nullptr)
	{
		const double error = local->get_updated_body()->get_global_position().distance_to(data["position"]);
		const String text = "client vs server " + String::num(error, 3) + " m";
		out += (error > 0.25 ? colored(COLOR_BAD, text) : dim(text));
		if (String(local->get_movement_mode()) != String(data.get("mode", "")))
		{
			out += "  " + colored(COLOR_BAD, "mode here: " + String(local->get_movement_mode()));
		}
		out += "\n";
	}
	return out;
}

void GameplayDebuggerCategoryMovement::draw_world(Node* target, const Dictionary& data)
{
	DebugDraw* draw = DebugDraw::get_singleton();
	if (draw == nullptr || data.is_empty()) { return; }

	if (bool(data.get("floor_hit", false)))
	{
		const Vector3 point = data["floor_point"];
		const Vector3 normal = data["floor_normal"];
		const Color color = bool(data["floor_walkable"]) ? Color(0.49, 0.78, 1.0) : Color(1, 0.54, 0.48);
		draw->sphere(point, 0.06, color);
		draw->arrow(point, point + normal * 0.6, color);
	}
	CharacterMovementComponent* local = Object::cast_to<CharacterMovementComponent>(find_component(target, "CharacterMovementComponent"));
	if (local != nullptr && local->get_updated_body() != nullptr)
	{
		const Vector3 position = local->get_updated_body()->get_global_position();
		const Vector3 velocity = local->get_velocity();
		if (velocity.length() > 0.05) { draw->arrow(position, position + velocity * 0.3, Color(0.56, 0.89, 0.53)); }
	}
}

// --- Net ----------------------------------------------------------------------

GameplayDebuggerCategoryNet::GameplayDebuggerCategoryNet()
{
	set_category_name("Net");
	set_enabled_by_default(false);
}

Dictionary GameplayDebuggerCategoryNet::collect_data(Node* target, PlayerController* viewer)
{
	Dictionary data;
	SceneTree* tree = viewer != nullptr ? viewer->get_tree() : (target != nullptr ? target->get_tree() : nullptr);
	if (tree == nullptr) { return data; }

	data["server_fps"] = Engine::get_singleton()->get_frames_per_second();
	data["time_scale"] = Engine::get_singleton()->get_time_scale();
	data["node_count"] = tree->get_node_count();

	Array peers;
	Ref<MultiplayerAPI> multiplayer = tree->get_multiplayer();
	Ref<ENetMultiplayerPeer> enet = multiplayer.is_valid() ? Ref<ENetMultiplayerPeer>(multiplayer->get_multiplayer_peer()) : Ref<ENetMultiplayerPeer>();
	if (enet.is_valid() && multiplayer->is_server())
	{
		const PackedInt32Array ids = multiplayer->get_peers();
		for (int i = 0; i < ids.size(); i++)
		{
			Ref<ENetPacketPeer> peer = enet->get_peer(ids[i]);
			Dictionary entry;
			entry["id"] = ids[i];
			entry["rtt"] = peer.is_valid() ? peer->get_statistic(ENetPacketPeer::PEER_ROUND_TRIP_TIME) : -1.0;
			entry["loss"] = peer.is_valid() ? peer->get_statistic(ENetPacketPeer::PEER_PACKET_LOSS) / double(ENetPacketPeer::PACKET_LOSS_SCALE) * 100.0 : 0.0;
			peers.push_back(entry);
		}
	}
	data["peers"] = peers;
	if (viewer != nullptr) { data["viewer_peer"] = viewer->get_owner_peer_id(); }
	return data;
}

String GameplayDebuggerCategoryNet::draw_data(Node* target, const Dictionary& data)
{
	if (data.is_empty()) { return dim("-"); }

	String out = "server " + String::num(double(data.get("server_fps", 0)), 0) + " fps   time scale "
		+ String::num(double(data.get("time_scale", 1.0)), 2) + "   nodes " + String::num_int64(data.get("node_count", 0)) + "\n";
	out += "here " + String::num(Engine::get_singleton()->get_frames_per_second(), 0) + " fps";
	SceneTree* tree = target != nullptr ? target->get_tree() : nullptr;
	if (tree != nullptr && tree->get_multiplayer().is_valid() && tree->get_multiplayer()->has_multiplayer_peer())
	{
		out += "   peer id " + String::num_int64(tree->get_multiplayer()->get_unique_id());
	}
	out += "\n";

	const Array peers = data.get("peers", Array());
	const int viewer = data.get("viewer_peer", 0);
	for (int i = 0; i < peers.size(); i++)
	{
		const Dictionary peer = peers[i];
		const int id = peer["id"];
		const double rtt = peer["rtt"];
		const String line = "peer " + String::num_int64(id) + "  rtt " + String::num(rtt, 0) + " ms  loss " + String::num(double(peer["loss"]), 1) + "%"
			+ (id == viewer ? String("  (you)") : String());
		out += (rtt > 150 ? colored(COLOR_BAD, line) : line) + String("\n");
	}
	if (peers.is_empty()) { out += dim("no remote peers") + "\n"; }
	return out;
}
