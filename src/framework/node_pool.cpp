#include "framework/node_pool.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace GFGD
{
NodePool::NodePool()
{
	prewarm = 0;
}

NodePool::~NodePool()
{

}

void NodePool::_ready()
{
	for (int i = 0; i < prewarm; i++)
	{
		Node* node = create_node();
		if (node == nullptr) { return; }

		const int64_t instance_id = (int64_t)node->get_instance_id();
		owned.push_back(instance_id);
		free_nodes.push_back(instance_id);
	}
}

void NodePool::_exit_tree()
{
	clear();
}

Node* NodePool::acquire()
{
	Node* node = nullptr;

	while (node == nullptr && !free_nodes.is_empty())
	{
		const int64_t instance_id = free_nodes[free_nodes.size() - 1];
		free_nodes.remove_at(free_nodes.size() - 1);

		node = node_from_id(instance_id);
		if (node == nullptr)
		{
			// Freed behind the pool's back. Drop it from the owned list too,
			// or get_total_created() would count it forever.
			erase_id(owned, instance_id);
		}
	}

	if (node == nullptr)
	{
		node = create_node();
		if (node == nullptr) { return nullptr; }

		owned.push_back((int64_t)node->get_instance_id());
	}

	if (node->get_parent() == nullptr)
	{
		resolve_container()->add_child(node);
	}

	notify_pooled(node, "_on_acquired");
	emit_signal("node_acquired", node);
	return node;
}

void NodePool::release(Node* node)
{
	if (node == nullptr) { return; }

	const int64_t instance_id = (int64_t)node->get_instance_id();

	// Removal from the tree is deferred, so a node released this frame is still
	// parented and still reachable. Without this guard a second release would run
	// the node's cleanup twice and hand the same node out twice.
	if (free_nodes.has(instance_id)) { return; }

	notify_pooled(node, "_on_released");
	free_nodes.push_back(instance_id);

	if (node->get_parent() != nullptr)
	{
		call_deferred("detach_deferred", instance_id);
	}

	emit_signal("node_released", node);
}

void NodePool::release_all()
{
	for (int i = 0; i < owned.size(); i++)
	{
		Node* node = node_from_id(owned[i]);
		if (node != nullptr && node->get_parent() != nullptr)
		{
			release(node);
		}
	}
}

void NodePool::clear()
{
	for (int i = 0; i < owned.size(); i++)
	{
		Node* node = node_from_id(owned[i]);
		if (node != nullptr) { node->queue_free(); }
	}

	owned.clear();
	free_nodes.clear();
}

TypedArray<Node> NodePool::get_active() const
{
	TypedArray<Node> active;

	for (int i = 0; i < owned.size(); i++)
	{
		if (free_nodes.has(owned[i])) { continue; }

		Node* node = node_from_id(owned[i]);
		if (node != nullptr) { active.push_back(node); }
	}

	return active;
}

int NodePool::get_active_count() const
{
	int count = 0;

	for (int i = 0; i < owned.size(); i++)
	{
		if (free_nodes.has(owned[i])) { continue; }
		if (node_from_id(owned[i]) != nullptr) { count++; }
	}

	return count;
}

Node* NodePool::create_node()
{
	if (factory.is_valid())
	{
		const Variant result = factory.call();
		Node* node = Object::cast_to<Node>(result);
		if (node == nullptr)
		{
			ERR_PRINT("GFGD: NodePool factory did not return a Node.");
		}
		return node;
	}

	if (scene.is_valid())
	{
		return scene->instantiate();
	}

	ERR_PRINT(vformat("GFGD: NodePool '%s' has neither a scene nor a factory, so it cannot create anything.", get_name()));
	return nullptr;
}

Node* NodePool::resolve_container()
{
	if (!container.is_empty())
	{
		Node* node = get_node_or_null(container);
		if (node != nullptr) { return node; }

		WARN_PRINT(vformat("GFGD: NodePool '%s' cannot reach its container '%s'. Parenting to the pool instead.", get_name(), String(container)));
	}

	return this;
}

void NodePool::notify_pooled(Node* node, const StringName& method) const
{
	if (node->has_method(method))
	{
		node->call(method);
	}
}

void NodePool::detach_deferred(int64_t instance_id)
{
	Node* node = node_from_id(instance_id);
	if (node == nullptr) { return; }

	Node* parent = node->get_parent();
	if (parent != nullptr) { parent->remove_child(node); }
}

Node* NodePool::node_from_id(int64_t instance_id)
{
	return Object::cast_to<Node>(UtilityFunctions::instance_from_id(instance_id));
}

void NodePool::erase_id(PackedInt64Array& ids, int64_t instance_id)
{
	const int64_t index = ids.find(instance_id);
	if (index >= 0) { ids.remove_at((int)index); }
}

void NodePool::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("acquire"), &NodePool::acquire);
	ClassDB::bind_method(D_METHOD("release", "node"), &NodePool::release);
	ClassDB::bind_method(D_METHOD("detach_deferred", "instance_id"), &NodePool::detach_deferred);
	ClassDB::bind_method(D_METHOD("release_all"), &NodePool::release_all);
	ClassDB::bind_method(D_METHOD("clear"), &NodePool::clear);
	ClassDB::bind_method(D_METHOD("get_active"), &NodePool::get_active);
	ClassDB::bind_method(D_METHOD("get_active_count"), &NodePool::get_active_count);
	ClassDB::bind_method(D_METHOD("get_total_created"), &NodePool::get_total_created);

	ClassDB::bind_method(D_METHOD("get_scene"), &NodePool::get_scene);
	ClassDB::bind_method(D_METHOD("set_scene", "value"), &NodePool::set_scene);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_scene", "get_scene");

	ClassDB::bind_method(D_METHOD("get_factory"), &NodePool::get_factory);
	ClassDB::bind_method(D_METHOD("set_factory", "value"), &NodePool::set_factory);
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "factory", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_factory", "get_factory");

	ClassDB::bind_method(D_METHOD("get_prewarm"), &NodePool::get_prewarm);
	ClassDB::bind_method(D_METHOD("set_prewarm", "value"), &NodePool::set_prewarm);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "prewarm", PROPERTY_HINT_RANGE, "0,256,1,or_greater"), "set_prewarm", "get_prewarm");

	ClassDB::bind_method(D_METHOD("get_container"), &NodePool::get_container);
	ClassDB::bind_method(D_METHOD("set_container", "value"), &NodePool::set_container);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "container"), "set_container", "get_container");

	ADD_SIGNAL(MethodInfo("node_acquired", PropertyInfo(Variant::OBJECT, "node", PROPERTY_HINT_NODE_TYPE, "Node")));
	ADD_SIGNAL(MethodInfo("node_released", PropertyInfo(Variant::OBJECT, "node", PROPERTY_HINT_NODE_TYPE, "Node")));
}
}
