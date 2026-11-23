/**************************************************************************/
/*  ipde_nodes.h                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/label.h"

class Camera2D;
class Camera3D;

// IPDE ready-made nodes.
// None of them animate inside the editor, so they never change what gets saved in a scene.

// Shows the frames per second (and optionally frame time and memory usage).
class FPSCounter : public Label {
	GDCLASS(FPSCounter, Label);

	double update_interval = 0.25;
	bool show_frame_time = false;
	bool show_memory = false;
	bool color_by_performance = true;
	double accumulated = 0.0;

	void _update_text();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_update_interval(double p_interval);
	double get_update_interval() const;
	void set_show_frame_time(bool p_enabled);
	bool is_showing_frame_time() const;
	void set_show_memory(bool p_enabled);
	bool is_showing_memory() const;
	void set_color_by_performance(bool p_enabled);
	bool is_color_by_performance() const;

	FPSCounter();
};

// Rotates continuously (coins, pickups, fans, platforms...).
class AutoRotate3D : public Node3D {
	GDCLASS(AutoRotate3D, Node3D);

	Vector3 rotation_speed = Vector3(0, 90, 0); // Degrees per second.
	bool local_space = true;
	bool active = true;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_rotation_speed(const Vector3 &p_speed);
	Vector3 get_rotation_speed() const;
	void set_local_space(bool p_local);
	bool is_local_space() const;
	void set_active(bool p_active);
	bool is_active() const;

	AutoRotate3D();
};

class AutoRotate2D : public Node2D {
	GDCLASS(AutoRotate2D, Node2D);

	double rotation_speed = 90.0; // Degrees per second.
	bool active = true;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_rotation_speed(double p_speed);
	double get_rotation_speed() const;
	void set_active(bool p_active);
	bool is_active() const;

	AutoRotate2D();
};

// Floats smoothly around its starting position (pickups, floating objects...).
class Bobbing3D : public Node3D {
	GDCLASS(Bobbing3D, Node3D);

	Vector3 amplitude = Vector3(0, 0.25, 0);
	double frequency = 0.5; // Cycles per second.
	double phase = 0.0; // 0..1
	bool active = true;

	Vector3 base_position;
	double time = 0.0;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_amplitude(const Vector3 &p_amplitude);
	Vector3 get_amplitude() const;
	void set_frequency(double p_frequency);
	double get_frequency() const;
	void set_phase(double p_phase);
	double get_phase() const;
	void set_active(bool p_active);
	bool is_active() const;
	void reset_base_position();

	Bobbing3D();
};

// Trauma-based camera shake. Add it as a child of a camera (or set camera_path) and call shake().
class ScreenShakeBase : public Node {
	GDCLASS(ScreenShakeBase, Node);

protected:
	NodePath camera_path;
	double trauma = 0.0;
	double decay = 1.5; // Trauma lost per second.
	double frequency = 25.0;
	double time = 0.0;
	uint32_t seed = 0;

	double _noise(double p_t, uint32_t p_channel) const;
	double _get_shake_amount() const { return trauma * trauma; }
	virtual void _apply_shake(double p_amount, double p_t) {}
	virtual void _reset_shake() {}

	void _notification(int p_what);
	static void _bind_methods();

public:
	void shake(double p_strength = 0.5);
	void stop();
	void set_camera_path(const NodePath &p_path);
	NodePath get_camera_path() const;
	void set_trauma(double p_trauma);
	double get_trauma() const;
	void set_decay(double p_decay);
	double get_decay() const;
	void set_frequency(double p_frequency);
	double get_frequency() const;

	ScreenShakeBase();
};

class ScreenShake2D : public ScreenShakeBase {
	GDCLASS(ScreenShake2D, ScreenShakeBase);

	Vector2 max_offset = Vector2(24, 16);
	Vector2 applied_offset;

	Camera2D *_get_camera() const;

protected:
	virtual void _apply_shake(double p_amount, double p_t) override;
	virtual void _reset_shake() override;
	static void _bind_methods();

public:
	void set_max_offset(const Vector2 &p_offset);
	Vector2 get_max_offset() const;
};

class ScreenShake3D : public ScreenShakeBase {
	GDCLASS(ScreenShake3D, ScreenShakeBase);

	Vector2 max_offset = Vector2(0.3, 0.2);
	Vector2 applied_offset;

	Camera3D *_get_camera() const;

protected:
	virtual void _apply_shake(double p_amount, double p_t) override;
	virtual void _reset_shake() override;
	static void _bind_methods();

public:
	void set_max_offset(const Vector2 &p_offset);
	Vector2 get_max_offset() const;
};
