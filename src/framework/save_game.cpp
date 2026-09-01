#include "framework/save_game.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/json.hpp>

using namespace godot;

namespace GFGD
{
SaveGame::SaveGame()
{

}

SaveGame::~SaveGame()
{

}

String SaveGame::to_json()
{
	String script_result;
	if (GDVIRTUAL_CALL(_to_json, script_result))
	{
		return script_result;
	}

	Dictionary data;
	TypedArray<Dictionary> properties = get_property_list();
	for (int i = 0; i < properties.size(); i++)
	{
		Dictionary property = properties[i];
		int usage = property["usage"];
		if ((usage & PROPERTY_USAGE_SCRIPT_VARIABLE) && (usage & PROPERTY_USAGE_STORAGE))
		{
			String property_name = property["name"];
			data[property_name] = get(property_name);
		}
	}
	return JSON::stringify(data);
}

Error SaveGame::from_json(const Variant& data)
{
	int script_result = OK;
	if (GDVIRTUAL_CALL(_from_json, data, script_result))
	{
		return (Error)script_result;
	}

	if (data.get_type() != Variant::DICTIONARY)
	{
		return ERR_INVALID_DATA;
	}

	Dictionary dictionary = data;
	Array keys = dictionary.keys();
	for (int i = 0; i < keys.size(); i++)
	{
		set(keys[i], dictionary[keys[i]]);
	}
	return OK;
}

void SaveGame::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("to_json"), &SaveGame::to_json);
	ClassDB::bind_method(D_METHOD("from_json", "data"), &SaveGame::from_json);

	GDVIRTUAL_BIND(_to_json);
	GDVIRTUAL_BIND(_from_json, "data");
}
}
