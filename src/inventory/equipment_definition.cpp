#include "inventory/equipment_definition.h"

#include <godot_cpp/core/class_db.hpp>

namespace GFGD
{
void EquipmentActorToSpawn::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_scene", "value"), &EquipmentActorToSpawn::set_scene);
	ClassDB::bind_method(D_METHOD("get_scene"), &EquipmentActorToSpawn::get_scene);
	ClassDB::bind_method(D_METHOD("set_attach_to", "value"), &EquipmentActorToSpawn::set_attach_to);
	ClassDB::bind_method(D_METHOD("get_attach_to"), &EquipmentActorToSpawn::get_attach_to);
	ClassDB::bind_method(D_METHOD("set_attach_bone", "value"), &EquipmentActorToSpawn::set_attach_bone);
	ClassDB::bind_method(D_METHOD("get_attach_bone"), &EquipmentActorToSpawn::get_attach_bone);
	ClassDB::bind_method(D_METHOD("set_attach_transform", "value"), &EquipmentActorToSpawn::set_attach_transform);
	ClassDB::bind_method(D_METHOD("get_attach_transform"), &EquipmentActorToSpawn::get_attach_transform);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_scene", "get_scene");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "attach_to"), "set_attach_to", "get_attach_to");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "attach_bone"), "set_attach_bone", "get_attach_bone");
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "attach_transform"), "set_attach_transform", "get_attach_transform");
}

void EquipmentDefinition::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_instance_script", "value"), &EquipmentDefinition::set_instance_script);
	ClassDB::bind_method(D_METHOD("get_instance_script"), &EquipmentDefinition::get_instance_script);
	ClassDB::bind_method(D_METHOD("set_abilities_to_grant", "value"), &EquipmentDefinition::set_abilities_to_grant);
	ClassDB::bind_method(D_METHOD("get_abilities_to_grant"), &EquipmentDefinition::get_abilities_to_grant);
	ClassDB::bind_method(D_METHOD("set_effects_to_apply", "value"), &EquipmentDefinition::set_effects_to_apply);
	ClassDB::bind_method(D_METHOD("get_effects_to_apply"), &EquipmentDefinition::get_effects_to_apply);
	ClassDB::bind_method(D_METHOD("set_granted_tags", "value"), &EquipmentDefinition::set_granted_tags);
	ClassDB::bind_method(D_METHOD("get_granted_tags"), &EquipmentDefinition::get_granted_tags);
	ClassDB::bind_method(D_METHOD("set_actors_to_spawn", "value"), &EquipmentDefinition::set_actors_to_spawn);
	ClassDB::bind_method(D_METHOD("get_actors_to_spawn"), &EquipmentDefinition::get_actors_to_spawn);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "instance_script", PROPERTY_HINT_RESOURCE_TYPE, "Script"), "set_instance_script", "get_instance_script");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "abilities_to_grant", PROPERTY_HINT_ARRAY_TYPE, vformat("%d/%d:GameplayAbility", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_abilities_to_grant", "get_abilities_to_grant");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "effects_to_apply", PROPERTY_HINT_ARRAY_TYPE, vformat("%d/%d:GameplayEffect", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_effects_to_apply", "get_effects_to_apply");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "granted_tags"), "set_granted_tags", "get_granted_tags");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "actors_to_spawn", PROPERTY_HINT_ARRAY_TYPE, vformat("%d/%d:EquipmentActorToSpawn", Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE)), "set_actors_to_spawn", "get_actors_to_spawn");
}
}
