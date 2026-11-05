#include "inventory/equipment_instance.h"

#include <godot_cpp/classes/bone_attachment3d.hpp>
#include <godot_cpp/classes/canvas_item.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/core/class_db.hpp>

#include "inventory/equipment_manager_component.h"

namespace GFGD
{
namespace
{
Skeleton3D* find_skeleton_with_bone(Node* node, const StringName& bone)
{
	if (node == nullptr) { return nullptr; }
	if (Skeleton3D* skeleton = Object::cast_to<Skeleton3D>(node))
	{
		if (skeleton->find_bone(bone) >= 0) { return skeleton; }
	}
	const int count = node->get_child_count();
	for (int i = 0; i < count; i++)
	{
		if (Skeleton3D* found = find_skeleton_with_bone(node->get_child(i), bone)) { return found; }
	}
	return nullptr;
}
}

Node* EquipmentInstance::get_pawn() const
{
	return Object::cast_to<Node>(ObjectDB::get_instance(pawn_root_id));
}

EquipmentManagerComponent* EquipmentInstance::get_manager() const
{
	return Object::cast_to<EquipmentManagerComponent>(ObjectDB::get_instance(manager_id));
}

Array EquipmentInstance::get_spawned_nodes() const
{
	Array nodes;
	for (int i = 0; i < spawned_node_ids.size(); i++)
	{
		if (Object* node = ObjectDB::get_instance(ObjectID(uint64_t(spawned_node_ids[i])))) { nodes.push_back(node); }
	}
	return nodes;
}

Node* EquipmentInstance::resolve_attach_parent(const Ref<EquipmentActorToSpawn>& spawn)
{
	Node* custom = nullptr;
	if (GDVIRTUAL_CALL(_get_attach_parent, spawn, custom) && custom != nullptr) { return custom; }

	Node* pawn = get_pawn();
	if (pawn == nullptr) { return nullptr; }
	Node* target = spawn->get_attach_to().is_empty() ? pawn : pawn->get_node_or_null(spawn->get_attach_to());
	if (target == nullptr)
	{
		WARN_PRINT(vformat("GFGD: equipment attach_to \"%s\" not found under %s.", String(spawn->get_attach_to()), String(pawn->get_path())));
		return nullptr;
	}
	if (spawn->get_attach_bone() == StringName()) { return target; }

	Skeleton3D* skeleton = find_skeleton_with_bone(target, spawn->get_attach_bone());
	if (skeleton == nullptr)
	{
		WARN_PRINT(vformat("GFGD: no Skeleton3D with bone \"%s\" under %s.", String(spawn->get_attach_bone()), String(target->get_path())));
		return nullptr;
	}
	// A bone attachment of its own, freed with the scene, so equipment never
	// leaves anything behind on the skeleton.
	BoneAttachment3D* attachment = memnew(BoneAttachment3D);
	attachment->set_name(vformat("Equipment%d_%s", equipment_id, String(spawn->get_attach_bone()).replace(".", "_")));
	skeleton->add_child(attachment);
	attachment->set_bone_name(spawn->get_attach_bone());
	spawned_node_ids.push_back(int64_t(uint64_t(attachment->get_instance_id())));
	return attachment;
}

void EquipmentInstance::spawn_equipment_actors()
{
	if (definition.is_null()) { return; }
	const TypedArray<EquipmentActorToSpawn> spawns = definition->get_actors_to_spawn();
	for (int i = 0; i < spawns.size(); i++)
	{
		Ref<EquipmentActorToSpawn> spawn = spawns[i];
		if (spawn.is_null() || spawn->get_scene().is_null()) { continue; }

		Node* parent = resolve_attach_parent(spawn);
		if (parent == nullptr) { continue; }

		Node* node = spawn->get_scene()->instantiate();
		if (node == nullptr) { continue; }
		const Transform3D transform = spawn->get_attach_transform();
		if (Node3D* node_3d = Object::cast_to<Node3D>(node))
		{
			node_3d->set_transform(transform);
		}
		else if (Node2D* node_2d = Object::cast_to<Node2D>(node))
		{
			node_2d->set_position(Vector2(transform.origin.x, transform.origin.y));
		}
		parent->add_child(node);
		spawned_node_ids.push_back(int64_t(uint64_t(node->get_instance_id())));
	}
	set_spawned_visible(spawned_visible);
}

void EquipmentInstance::destroy_equipment_actors()
{
	for (int i = 0; i < spawned_node_ids.size(); i++)
	{
		if (Node* node = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(uint64_t(spawned_node_ids[i])))))
		{
			node->queue_free();
		}
	}
	spawned_node_ids.clear();
}

void EquipmentInstance::set_spawned_visible(bool value)
{
	spawned_visible = value;
	for (int i = 0; i < spawned_node_ids.size(); i++)
	{
		Object* object = ObjectDB::get_instance(ObjectID(uint64_t(spawned_node_ids[i])));
		if (Node3D* node_3d = Object::cast_to<Node3D>(object)) { node_3d->set_visible(value); }
		else if (CanvasItem* item = Object::cast_to<CanvasItem>(object)) { item->set_visible(value); }
	}
}

void EquipmentInstance::on_equipped()
{
	GDVIRTUAL_CALL(_on_equipped);
}

void EquipmentInstance::on_unequipped()
{
	GDVIRTUAL_CALL(_on_unequipped);
}

void EquipmentInstance::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("get_equipment_id"), &EquipmentInstance::get_equipment_id);
	ClassDB::bind_method(D_METHOD("get_item_id"), &EquipmentInstance::get_item_id);
	ClassDB::bind_method(D_METHOD("get_equipment_definition"), &EquipmentInstance::get_equipment_definition);
	ClassDB::bind_method(D_METHOD("get_item_definition"), &EquipmentInstance::get_item_definition);
	ClassDB::bind_method(D_METHOD("get_instigator"), &EquipmentInstance::get_instigator);
	ClassDB::bind_method(D_METHOD("get_pawn"), &EquipmentInstance::get_pawn);
	ClassDB::bind_method(D_METHOD("get_manager"), &EquipmentInstance::get_manager);
	ClassDB::bind_method(D_METHOD("get_spawned_nodes"), &EquipmentInstance::get_spawned_nodes);
	ClassDB::bind_method(D_METHOD("set_spawned_visible", "value"), &EquipmentInstance::set_spawned_visible);
	ClassDB::bind_method(D_METHOD("is_spawned_visible"), &EquipmentInstance::is_spawned_visible);

	GDVIRTUAL_BIND(_on_equipped);
	GDVIRTUAL_BIND(_on_unequipped);
	GDVIRTUAL_BIND(_get_attach_parent, "spawn");
}
}
