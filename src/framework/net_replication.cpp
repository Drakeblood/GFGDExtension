#include "framework/net_replication.h"
#include "framework/delta_replicator.h"

#include <godot_cpp/classes/scene_replication_config.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "framework/net_driver.h"
#include "framework/world.h"

using namespace godot;

namespace GFGD
{
namespace Replication
{
MultiplayerSynchronizer* attach(Node* owner, Node* target, const PackedStringArray& properties, int authority_peer, const String& synchronizer_name)
{
	if (owner == nullptr || target == nullptr || properties.is_empty()) { return nullptr; }

	if (MultiplayerSynchronizer* existing = Object::cast_to<MultiplayerSynchronizer>(owner->get_node_or_null(NodePath(synchronizer_name))))
	{
		return existing;
	}

	Ref<SceneReplicationConfig> config;
	config.instantiate();
	for (int i = 0; i < properties.size(); i++)
	{
		// ".:name" is a property of the synchronizer's root, which is what
		// root_path below points at.
		const NodePath property_path = NodePath(vformat(".:%s", properties[i]));
		config->add_property(property_path);
		config->property_set_spawn(property_path, true);
		config->property_set_sync(property_path, true);
	}

	MultiplayerSynchronizer* synchronizer = memnew(MultiplayerSynchronizer);
	synchronizer->set_name(synchronizer_name);
	synchronizer->set_replication_config(config);

	// Relative to the synchronizer, ".." is the node it hangs off; anything
	// further is spelled out from there.
	String root_path = "..";
	if (target != owner)
	{
		root_path += "/" + String(owner->get_path_to(target));
	}
	synchronizer->set_root_path(NodePath(root_path));

	owner->add_child(synchronizer);

	// After add_child: entering the tree is what registers the synchronizer with
	// the multiplayer API, and it has to be registered under the id that sends.
	synchronizer->set_multiplayer_authority(authority_peer);

	// A peer is only sent this once it has the level up and has been handed the
	// nodes these values belong to. Without that it would be sent values for a
	// node it has never heard of, which is an error on its side and a wasted
	// packet on this one. Also after add_child: a filter added to a synchronizer
	// outside the tree is stored but never acted on.
	World* world = owner->is_inside_tree() ? Object::cast_to<World>(owner->get_tree()) : nullptr;
	NetDriver* net_driver = world != nullptr ? world->get_net_driver() : nullptr;
	if (net_driver != nullptr && world->has_authority())
	{
		// A filter can only take visibility away, never grant it, so the
		// synchronizer stays public and this narrows it down to ready peers.
		synchronizer->add_visibility_filter(Callable(net_driver, "is_peer_ready"));
	}

	return synchronizer;
}

Node* attach_delta(Node* owner, Node* target, const PackedStringArray& properties, const String& replicator_name, double check_interval)
{
	if (owner == nullptr || target == nullptr || properties.is_empty()) { return nullptr; }
	if (Node* existing = owner->get_node_or_null(NodePath(replicator_name))) { return existing; }

	// "orders:id" replicates orders as an array keyed by each element's "id".
	// "orders:id|push": keyed by "id", and push-based (compared only when marked).
	PackedStringArray names;
	PackedStringArray push;
	Dictionary keyed;
	for (int i = 0; i < properties.size(); i++)
	{
		const PackedStringArray options = properties[i].split("|");
		const PackedStringArray parts = options[0].split(":", true, 1);
		const String name = parts[0].strip_edges();
		names.push_back(name);
		if (parts.size() > 1 && !parts[1].strip_edges().is_empty())
		{
			keyed[StringName(name)] = StringName(parts[1].strip_edges());
		}
		for (int o = 1; o < options.size(); o++)
		{
			const String option = options[o].strip_edges();
			if (option == "push") { push.push_back(name); }
			else { WARN_PRINT(vformat("GFGD: unknown delta replication option \"%s\" on \"%s\" (known: push).", option, name)); }
		}
	}
	names = validate_properties(target, names);
	if (names.is_empty()) { return nullptr; }

	DeltaReplicator* replicator = memnew(DeltaReplicator);
	replicator->set_name(replicator_name);
	replicator->set_properties(names);
	replicator->set_keyed_arrays(keyed);
	replicator->set_push_properties(push);
	replicator->set_check_interval(check_interval);
	String root_path = "..";
	if (target != owner)
	{
		root_path += "/" + String(owner->get_path_to(target));
	}
	replicator->set_root_path(NodePath(root_path));
	owner->add_child(replicator);
	return replicator;
}

Node* attach_framework(Node* node, const PackedStringArray& own_push_properties, const PackedStringArray& script_properties, const PackedStringArray& delta_properties)
{
	PackedStringArray all;
	for (int i = 0; i < own_push_properties.size(); i++) { all.push_back(String(own_push_properties[i]) + String("|push")); }
	all.append_array(script_properties);
	all.append_array(delta_properties);
	return attach_delta(node, node, all, "Replication", 0.0);
}

PackedStringArray validate_properties(Node* node, const PackedStringArray& properties)
{
	PackedStringArray result;
	if (node == nullptr) { return result; }

	PackedStringArray known;
	TypedArray<Dictionary> property_list = node->get_property_list();
	for (int i = 0; i < property_list.size(); i++)
	{
		const Dictionary property = property_list[i];
		known.push_back(property.get("name", String()));
	}

	for (int i = 0; i < properties.size(); i++)
	{
		const String name = properties[i];
		if (name.is_empty() || result.has(name)) { continue; }

		if (!known.has(name))
		{
			WARN_PRINT(vformat("GFGD: \"%s\" is not a property of %s, so it will not be replicated.", name, node->get_class()));
			continue;
		}

		result.push_back(name);
	}

	return result;
}
}
}
