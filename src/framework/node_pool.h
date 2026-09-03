#ifndef NODE_POOL_H
#define NODE_POOL_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>

using namespace godot;

namespace GFGD
{
// Recycles nodes instead of instantiating them, for anything spawned by the
// hundred: bullets, debris, enemies.
//
// Pooling here means taking the node OUT OF THE TREE, not disabling it. A node
// outside the tree does not exist for the physics server - it cannot collide,
// it costs no broadphase budget, and a "dead" body cannot be hit by accident.
// The alternative (freeze + process_mode disabled) leaves the body in the world
// and is a reliable source of bugs that are very hard to find.
class NodePool : public Node
{
	GDCLASS(NodePool, Node)

private:
	Ref<PackedScene> scene;

	// Takes precedence over `scene`. Use it when a pooled node needs constructor
	// arguments or a script attached from code.
	Callable factory;

	int prewarm;

	// Where acquired nodes are parented. Empty means this node.
	NodePath container;

	// Every node this pool has ever created, and the subset currently available.
	// Both hold instance ids rather than pointers: a node freed behind the pool's
	// back then resolves to null instead of being dereferenced.
	PackedInt64Array owned;
	PackedInt64Array free_nodes;

public:
	NodePool();
	~NodePool();

	virtual void _ready() override;
	virtual void _exit_tree() override;

	Node* acquire();

	// Safe to call from inside a physics callback or a signal: the node is marked
	// free immediately, but leaving the tree is deferred to the end of the frame.
	void release(Node* node);

	// Returns everything currently in the tree. Used to reset a round without
	// reloading the scene.
	void release_all();

	// Frees every node this pool owns. Called on _exit_tree, because nodes
	// waiting in the free list have no parent and nobody else would free them.
	void clear();

	TypedArray<Node> get_active() const;
	int get_active_count() const;
	int get_total_created() const { return owned.size(); }

	Ref<PackedScene> get_scene() const { return scene; }
	void set_scene(const Ref<PackedScene>& value) { scene = value; }

	Callable get_factory() const { return factory; }
	void set_factory(const Callable& value) { factory = value; }

	int get_prewarm() const { return prewarm; }
	void set_prewarm(int value) { prewarm = value; }

	NodePath get_container() const { return container; }
	void set_container(const NodePath& value) { container = value; }

protected:
	static void _bind_methods();

private:
	Node* create_node();
	Node* resolve_container();

	// A pooled node is a RigidBody2D one moment and an Area2D the next, so there
	// is no base class to put these on - they are called when present.
	void notify_pooled(Node* node, const StringName& method) const;

	// Deferred by release(), and takes an id rather than the node itself: by the
	// time it runs the node may have been reparented or freed, and re-resolving
	// is what keeps either from turning into an error at the far end.
	void detach_deferred(int64_t instance_id);

	static Node* node_from_id(int64_t instance_id);
	static void erase_id(PackedInt64Array& ids, int64_t instance_id);
};
}

#endif
