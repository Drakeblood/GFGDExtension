#ifndef GFGD_DEBUG_DRAW_H
#define GFGD_DEBUG_DRAW_H

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/templates/vector.hpp>

using namespace godot;

namespace GFGD
{
class DebugDrawNode;

// Debug lines, shapes and text in the 3D world - an engine singleton, so
// DebugDraw.line(a, b, Color.RED, 2.0) from any script.
//
// duration 0 draws for one frame - call it every frame to keep a shape up;
// a positive duration keeps it that many seconds. Shapes are drawn on top of
// everything, unlit. Text is drawn in screen space at the projected point.
// Release builds draw nothing unless the debug tools are enabled there, and
// DebugDraw.enabled = false switches drawing off.
class DebugDraw : public Object
{
	GDCLASS(DebugDraw, Object)

public:
	struct Line
	{
		Vector3 from;
		Vector3 to;
		Color color;
		double expires;
		bool single_frame;
	};

	struct Text
	{
		Vector3 position;
		String text;
		Color color;
		double expires;
		bool single_frame;
	};

private:
	static DebugDraw* singleton;
	Vector<Line> lines;
	Vector<Text> texts;
	ObjectID node_id;
	bool enabled;
	bool node_pending;

public:
	DebugDraw();

	static DebugDraw* get_singleton() { return singleton; }
	static void create_singleton();
	static void destroy_singleton();

	void line(const Vector3& from, const Vector3& to, const Color& color = Color(1, 0, 0), double duration = 0.0);
	void arrow(const Vector3& from, const Vector3& to, const Color& color = Color(1, 0, 0), double duration = 0.0);
	void sphere(const Vector3& center, float radius, const Color& color = Color(1, 0, 0), double duration = 0.0);
	void box(const Vector3& center, const Vector3& size, const Color& color = Color(1, 0, 0), double duration = 0.0);
	void transformed_box(const Transform3D& transform, const Vector3& size, const Color& color = Color(1, 0, 0), double duration = 0.0);
	void point(const Vector3& position, float size = 0.1f, const Color& color = Color(1, 0, 0), double duration = 0.0);
	void axes(const Transform3D& transform, float size = 0.5f, double duration = 0.0);
	void text(const Vector3& position, const String& text, const Color& color = Color(1, 1, 1), double duration = 0.0);
	void clear();

	void set_enabled(bool value);
	bool is_enabled() const { return enabled; }

	// --- Used by the drawing node ----------------------------------------------
	const Vector<Line>& get_lines() const { return lines; }
	const Vector<Text>& get_texts() const { return texts; }
	// Drops what has expired and every single-frame shape that was just drawn.
	void end_frame();
	void node_ready(DebugDrawNode* node);

protected:
	static void _bind_methods();

private:
	bool accepting() const;
	double now() const;
	void ensure_node();
	void add_line(const Vector3& from, const Vector3& to, const Color& color, double duration);
};

// Renders DebugDraw's shapes: a MeshInstance3D with an ImmediateMesh rebuilt
// every frame, and a canvas for the text. Created on first use under the root.
class DebugDrawNode : public Node
{
	GDCLASS(DebugDrawNode, Node)

private:
	Ref<ImmediateMesh> mesh;
	Control* canvas;
	Vector<DebugDraw::Text> frame_texts;

public:
	DebugDrawNode();
	virtual void _ready() override;
	virtual void _process(double delta) override;

	const Vector<DebugDraw::Text>& get_frame_texts() const { return frame_texts; }

protected:
	static void _bind_methods() {}
};

class DebugDrawCanvas : public Control
{
	GDCLASS(DebugDrawCanvas, Control)

public:
	virtual void _draw() override;

protected:
	static void _bind_methods() {}
};
}

#endif
