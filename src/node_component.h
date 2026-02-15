#ifndef NODE_COMPONENT_H
#define NODE_COMPONENT_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/node.hpp>

using namespace godot;

namespace GFGD 
{
class NodeComponent : public Object
{
	GDCLASS(NodeComponent, Object)

private:
	Node* owner;

public:
	NodeComponent(Node* in_owner);
	~NodeComponent();

	template<typename T>
	T* get_owner() const { return Object::cast_to<T>(owner); }

	Node* get_owner_node() const { return owner; }

protected:
	static void _bind_methods();
};

}

#endif