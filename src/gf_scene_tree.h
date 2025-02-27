#ifndef GF_SCENE_TREE_H
#define GF_SCENE_TREE_H

#include <godot_cpp/classes/scene_tree.hpp>
#include "game_instance.h"

namespace godot
{
class GFSceneTree : public SceneTree
{
	GDCLASS(GFSceneTree, SceneTree)

private:
	GameInstance* gameInstance;

public:
	GFSceneTree();
	~GFSceneTree();

	virtual void _initialize() override;

	_FORCE_INLINE_ GameInstance* GetGameInstance() { return gameInstance; }
protected:
	static void _bind_methods();

};
}

#endif