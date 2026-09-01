#ifndef GAMEPLAY_TAG_H
#define GAMEPLAY_TAG_H

#include <godot_cpp/classes/resource.hpp>

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
	void set_tag_name(const StringName& value) { tag_name = value; }

	// "A.B.C" matches "A.B" (parent) and "A.B.C" (exact), but not "A.B.C.D".
	bool matches_tag(const Ref<GameplayTag>& tag_to_check) const;
	bool matches_tag_exact(const Ref<GameplayTag>& tag_to_check) const;

protected:
	static void _bind_methods();
};

}

#endif
