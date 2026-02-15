#ifndef GAMEPLAY_TAG_CONTAINER_H
#define GAMEPLAY_TAG_CONTAINER_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/array.hpp>

using namespace godot;

namespace GFGD 
{
class GameplayTag;

class GameplayTagContainer : public Resource
{
	GDCLASS(GameplayTagContainer, Resource)

private:
	Array gameplay_tags;

public:
	GameplayTagContainer();

	void add_tag(const Ref<GameplayTag>& tag);
	void remove_tag(const Ref<GameplayTag>& tag);

	bool has_tag(const Ref<GameplayTag>& tag_to_check) const;
	bool has_tag_exact(const Ref<GameplayTag>& tag_to_check) const;
	bool has_any(const Ref<GameplayTagContainer>& tag_container) const;
	bool has_all(const Ref<GameplayTagContainer>& tag_container) const;
	bool has_all_exact(const Ref<GameplayTagContainer>& tag_container) const;

	int get_length() const { return gameplay_tags.size(); }
	Ref<GameplayTag> get_tag(int index) const;

protected:
	static void _bind_methods();
};

}

#endif