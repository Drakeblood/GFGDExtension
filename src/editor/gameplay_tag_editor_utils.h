#ifndef GAMEPLAY_TAG_EDITOR_UTILS_H
#define GAMEPLAY_TAG_EDITOR_UTILS_H

#ifdef TOOLS_ENABLED

#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

using namespace godot;

namespace GFGD
{

// How a tag is annotated wherever the editor shows one, so the tree picker, the
// single-tag button and the container rows all report a tag's origin the same
// way.
namespace GameplayTagEditorUtils
{
// Tag name, where it was declared, and its description if the table carries one.
String build_tooltip(const StringName& tag_name);

// True for a tag no table declares - request_tag() conjured it, which normally
// means a typo or an asset that outlived the table declaring its tag.
bool is_undeclared(const StringName& tag_name);

Color get_warning_color();

// Every name that should appear as a row for the given tags: the tags themselves
// plus every ancestor, whether or not the source listed it. Sorted, so a parent
// always precedes its children and a single forward pass can build the tree.
// A non-empty filter keeps only matches, and the ancestors that lead to them.
PackedStringArray expand_with_ancestors(const PackedStringArray& tag_names, const String& filter = String());
}

}

#endif // TOOLS_ENABLED

#endif
