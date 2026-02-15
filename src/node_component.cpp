#include "node_component.h"
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace GFGD
{
NodeComponent::NodeComponent(Node* in_owner)
{
	owner = in_owner;
}

NodeComponent::~NodeComponent()
{
	
}

void NodeComponent::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_owner_node"), &NodeComponent::get_owner_node);
}
}