#ifndef GAMEPLAY_TAG_H
#define GAMEPLAY_TAG_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/string_name.hpp>

using namespace godot;

namespace GFGD 
{
class GameplayTag : public Resource
{
	GDCLASS(GameplayTag, Resource)

private:
	StringName tag_name;

public:
	GameplayTag();
	GameplayTag(const StringName& tag_name);

	StringName get_tag_name() const { return tag_name; }

	bool matches_tag(const Ref<GameplayTag>& tag_to_check) const;
	bool matches_sub_tags(const Ref<GameplayTag>& tag_to_check) const;

	bool operator==(const Ref<GameplayTag>& other) const;
	bool operator!=(const Ref<GameplayTag>& other) const;

protected:
	static void _bind_methods();
};

}

#endif