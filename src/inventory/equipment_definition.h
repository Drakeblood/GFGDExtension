#ifndef GFGD_EQUIPMENT_DEFINITION_H
#define GFGD_EQUIPMENT_DEFINITION_H

#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;

namespace GFGD
{
// A scene shown on the character while a piece of equipment is on: the axe in
// the hand, the backpack on the back. Purely visual and local - every peer
// instantiates its own copy - so it must not be a networked node.
class EquipmentActorToSpawn : public Resource
{
	GDCLASS(EquipmentActorToSpawn, Resource)

private:
	Ref<PackedScene> scene;
	NodePath attach_to;
	StringName attach_bone;
	Transform3D attach_transform;

public:
	void set_scene(const Ref<PackedScene>& value) { scene = value; }
	Ref<PackedScene> get_scene() const { return scene; }

	// Relative to the pawn root; empty means the pawn root itself.
	void set_attach_to(const NodePath& value) { attach_to = value; }
	NodePath get_attach_to() const { return attach_to; }

	// When set, the scene follows this bone of the first Skeleton3D found at or
	// below attach_to - so a skeleton built at runtime needs no fixed path.
	void set_attach_bone(const StringName& value) { attach_bone = value; }
	StringName get_attach_bone() const { return attach_bone; }

	// Applied to a Node3D scene; a Node2D one takes the origin's x and y.
	void set_attach_transform(const Transform3D& value) { attach_transform = value; }
	Transform3D get_attach_transform() const { return attach_transform; }

protected:
	static void _bind_methods();
};

// What putting an item on does to the pawn: the abilities it grants, the
// effects and loose tags that hold while it is on, and the scenes shown.
// Referenced from an item by InventoryFragmentEquippableItem.
//
// Like item definitions, one that should reach clients must be a saved resource.
class EquipmentDefinition : public Resource
{
	GDCLASS(EquipmentDefinition, Resource)

private:
	Ref<Script> instance_script;
	Array abilities_to_grant;
	Array effects_to_apply;
	PackedStringArray granted_tags;
	TypedArray<EquipmentActorToSpawn> actors_to_spawn;

public:
	// A script extending EquipmentInstance, for equip/unequip logic of its own.
	// Empty means a plain EquipmentInstance.
	void set_instance_script(const Ref<Script>& value) { instance_script = value; }
	Ref<Script> get_instance_script() const { return instance_script; }

	// GameplayAbility templates, granted on the server through the pawn's
	// AbilitySystemComponent and taken away on unequip.
	void set_abilities_to_grant(const Array& value) { abilities_to_grant = value; }
	Array get_abilities_to_grant() const { return abilities_to_grant; }

	// GameplayEffects applied while equipped and removed on unequip.
	void set_effects_to_apply(const Array& value) { effects_to_apply = value; }
	Array get_effects_to_apply() const { return effects_to_apply; }

	// Loose gameplay tags held while equipped (e.g. "State.Equipped.Axe").
	void set_granted_tags(const PackedStringArray& value) { granted_tags = value; }
	PackedStringArray get_granted_tags() const { return granted_tags; }

	void set_actors_to_spawn(const TypedArray<EquipmentActorToSpawn>& value) { actors_to_spawn = value; }
	TypedArray<EquipmentActorToSpawn> get_actors_to_spawn() const { return actors_to_spawn; }

protected:
	static void _bind_methods();
};
}

#endif
