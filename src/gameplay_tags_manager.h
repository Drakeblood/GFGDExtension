#ifndef GAMEPLAY_TAGS_MANAGER_H
#define GAMEPLAY_TAGS_MANAGER_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/hash_set.hpp>
#include <godot_cpp/classes/dictionary.hpp>

using namespace godot;

namespace GFGD 
{
class GameplayTag;

class GameplayTagsManager : public Object
{
	GDCLASS(GameplayTagsManager, Object)

private:
	static GameplayTagsManager* instance;
	HashSet<Ref<GameplayTag>> tags;
	Dictionary tags_with_sub_tags;

public:
	static GameplayTagsManager* get_singleton();
	static void destroy_singleton();

	GameplayTagsManager();
	~GameplayTagsManager();

	Ref<GameplayTag> get_tag(const StringName& tag_name) const;
	Array get_separated_tag(const Ref<GameplayTag>& tag) const;

protected:
	static void _bind_methods();

private:
	void initialize_tags();
};

}

#endif