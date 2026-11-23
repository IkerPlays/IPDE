/**************************************************************************/
/*  ipde_nodes.cpp                                                        */
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

#include "ipde_nodes.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/2d/camera_2d.h"
#include "scene/3d/camera_3d.h"

static bool _is_editor() {
	return Engine::get_singleton()->is_editor_hint();
}

/* FPSCounter */

void FPSCounter::_update_text() {
	const double fps = Engine::get_singleton()->get_frames_per_second();
	String text = vformat("FPS: %d", int(Math::round(fps)));
	if (show_frame_time) {
		text += vformat("\n%.2f ms", fps > 0 ? 1000.0 / fps : 0.0);
	}
	if (show_memory) {
		text += "\nRAM: " + String::humanize_size(OS::get_singleton()->get_static_memory_usage());
	}
	set_text(text);

	if (color_by_performance) {
		Color c = Color(0.45, 0.95, 0.5);
		if (fps < 30) {
			c = Color(1, 0.42, 0.42);
		} else if (fps < 55) {
			c = Color(1, 0.85, 0.3);
		}
		add_theme_color_override(SNAME("font_color"), c);
	} else {
		remove_theme_color_override(SNAME("font_color"));
	}
}

void FPSCounter::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_process(!_is_editor());
			if (_is_editor()) {
				set_text("FPS: 60");
			} else {
				_update_text();
			}
		} break;
		case NOTIFICATION_PROCESS: {
			accumulated += get_process_delta_time();
			if (accumulated >= update_interval) {
				accumulated = 0.0;
				_update_text();
			}
		} break;
	}
}

void FPSCounter::set_update_interval(double p_interval) {
	update_interval = MAX(0.01, p_interval);
}
double FPSCounter::get_update_interval() const {
	return update_interval;
}
void FPSCounter::set_show_frame_time(bool p_enabled) {
	show_frame_time = p_enabled;
}
bool FPSCounter::is_showing_frame_time() const {
	return show_frame_time;
}
void FPSCounter::set_show_memory(bool p_enabled) {
	show_memory = p_enabled;
}
bool FPSCounter::is_showing_memory() const {
	return show_memory;
}
void FPSCounter::set_color_by_performance(bool p_enabled) {
	color_by_performance = p_enabled;
}
bool FPSCounter::is_color_by_performance() const {
	return color_by_performance;
}

void FPSCounter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_update_interval", "interval"), &FPSCounter::set_update_interval);
	ClassDB::bind_method(D_METHOD("get_update_interval"), &FPSCounter::get_update_interval);
	ClassDB::bind_method(D_METHOD("set_show_frame_time", "enabled"), &FPSCounter::set_show_frame_time);
	ClassDB::bind_method(D_METHOD("is_showing_frame_time"), &FPSCounter::is_showing_frame_time);
	ClassDB::bind_method(D_METHOD("set_show_memory", "enabled"), &FPSCounter::set_show_memory);
	ClassDB::bind_method(D_METHOD("is_showing_memory"), &FPSCounter::is_showing_memory);
	ClassDB::bind_method(D_METHOD("set_color_by_performance", "enabled"), &FPSCounter::set_color_by_performance);
	ClassDB::bind_method(D_METHOD("is_color_by_performance"), &FPSCounter::is_color_by_performance);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "update_interval", PROPERTY_HINT_RANGE, "0.01,5,0.01,suffix:s"), "set_update_interval", "get_update_interval");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "show_frame_time"), "set_show_frame_time", "is_showing_frame_time");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "show_memory"), "set_show_memory", "is_showing_memory");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "color_by_performance"), "set_color_by_performance", "is_color_by_performance");
}

FPSCounter::FPSCounter() {
	set_mouse_filter(MOUSE_FILTER_IGNORE);
	add_theme_constant_override(SNAME("outline_size"), 4);
	add_theme_color_override(SNAME("font_outline_color"), Color(0, 0, 0, 0.8));
	set_text("FPS: --");
}

/* AutoRotate3D */

void AutoRotate3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_process(!_is_editor());
		} break;
		case NOTIFICATION_PROCESS: {
			if (!active) {
				return;
			}
			const Vector3 step = rotation_speed * (Math::PI / 180.0) * get_process_delta_time();
			if (local_space) {
				if (step.x != 0) {
					rotate_object_local(Vector3(1, 0, 0), step.x);
				}
				if (step.y != 0) {
					rotate_object_local(Vector3(0, 1, 0), step.y);
				}
				if (step.z != 0) {
					rotate_object_local(Vector3(0, 0, 1), step.z);
				}
			} else {
				if (step.x != 0) {
					rotate(Vector3(1, 0, 0), step.x);
				}
				if (step.y != 0) {
					rotate(Vector3(0, 1, 0), step.y);
				}
				if (step.z != 0) {
					rotate(Vector3(0, 0, 1), step.z);
				}
			}
		} break;
	}
}

void AutoRotate3D::set_rotation_speed(const Vector3 &p_speed) {
	rotation_speed = p_speed;
}
Vector3 AutoRotate3D::get_rotation_speed() const {
	return rotation_speed;
}
void AutoRotate3D::set_local_space(bool p_local) {
	local_space = p_local;
}
bool AutoRotate3D::is_local_space() const {
	return local_space;
}
void AutoRotate3D::set_active(bool p_active) {
	active = p_active;
}
bool AutoRotate3D::is_active() const {
	return active;
}

void AutoRotate3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_rotation_speed", "speed"), &AutoRotate3D::set_rotation_speed);
	ClassDB::bind_method(D_METHOD("get_rotation_speed"), &AutoRotate3D::get_rotation_speed);
	ClassDB::bind_method(D_METHOD("set_local_space", "local"), &AutoRotate3D::set_local_space);
	ClassDB::bind_method(D_METHOD("is_local_space"), &AutoRotate3D::is_local_space);
	ClassDB::bind_method(D_METHOD("set_active", "active"), &AutoRotate3D::set_active);
	ClassDB::bind_method(D_METHOD("is_active"), &AutoRotate3D::is_active);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "rotation_speed", PROPERTY_HINT_NONE, "suffix:°/s"), "set_rotation_speed", "get_rotation_speed");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "local_space"), "set_local_space", "is_local_space");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active"), "set_active", "is_active");
}

AutoRotate3D::AutoRotate3D() {}

/* AutoRotate2D */

void AutoRotate2D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_process(!_is_editor());
		} break;
		case NOTIFICATION_PROCESS: {
			if (active && rotation_speed != 0.0) {
				rotate(rotation_speed * (Math::PI / 180.0) * get_process_delta_time());
			}
		} break;
	}
}

void AutoRotate2D::set_rotation_speed(double p_speed) {
	rotation_speed = p_speed;
}
double AutoRotate2D::get_rotation_speed() const {
	return rotation_speed;
}
void AutoRotate2D::set_active(bool p_active) {
	active = p_active;
}
bool AutoRotate2D::is_active() const {
	return active;
}

void AutoRotate2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_rotation_speed", "speed"), &AutoRotate2D::set_rotation_speed);
	ClassDB::bind_method(D_METHOD("get_rotation_speed"), &AutoRotate2D::get_rotation_speed);
	ClassDB::bind_method(D_METHOD("set_active", "active"), &AutoRotate2D::set_active);
	ClassDB::bind_method(D_METHOD("is_active"), &AutoRotate2D::is_active);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_speed", PROPERTY_HINT_RANGE, "-3600,3600,0.1,or_greater,or_less,suffix:°/s"), "set_rotation_speed", "get_rotation_speed");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active"), "set_active", "is_active");
}

AutoRotate2D::AutoRotate2D() {}

/* Bobbing3D */

void Bobbing3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			base_position = get_position();
			set_process(!_is_editor());
		} break;
		case NOTIFICATION_PROCESS: {
			if (!active) {
				return;
			}
			time += get_process_delta_time();
			const double s = Math::sin(Math::TAU * (time * frequency + phase));
			set_position(base_position + amplitude * s);
		} break;
	}
}

void Bobbing3D::set_amplitude(const Vector3 &p_amplitude) {
	amplitude = p_amplitude;
}
Vector3 Bobbing3D::get_amplitude() const {
	return amplitude;
}
void Bobbing3D::set_frequency(double p_frequency) {
	frequency = p_frequency;
}
double Bobbing3D::get_frequency() const {
	return frequency;
}
void Bobbing3D::set_phase(double p_phase) {
	phase = p_phase;
}
double Bobbing3D::get_phase() const {
	return phase;
}
void Bobbing3D::set_active(bool p_active) {
	active = p_active;
}
bool Bobbing3D::is_active() const {
	return active;
}
void Bobbing3D::reset_base_position() {
	base_position = get_position();
	time = 0.0;
}

void Bobbing3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_amplitude", "amplitude"), &Bobbing3D::set_amplitude);
	ClassDB::bind_method(D_METHOD("get_amplitude"), &Bobbing3D::get_amplitude);
	ClassDB::bind_method(D_METHOD("set_frequency", "frequency"), &Bobbing3D::set_frequency);
	ClassDB::bind_method(D_METHOD("get_frequency"), &Bobbing3D::get_frequency);
	ClassDB::bind_method(D_METHOD("set_phase", "phase"), &Bobbing3D::set_phase);
	ClassDB::bind_method(D_METHOD("get_phase"), &Bobbing3D::get_phase);
	ClassDB::bind_method(D_METHOD("set_active", "active"), &Bobbing3D::set_active);
	ClassDB::bind_method(D_METHOD("is_active"), &Bobbing3D::is_active);
	ClassDB::bind_method(D_METHOD("reset_base_position"), &Bobbing3D::reset_base_position);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "amplitude", PROPERTY_HINT_NONE, "suffix:m"), "set_amplitude", "get_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frequency", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:Hz"), "set_frequency", "get_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "phase", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_phase", "get_phase");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active"), "set_active", "is_active");
}

Bobbing3D::Bobbing3D() {}

/* ScreenShakeBase */

double ScreenShakeBase::_noise(double p_t, uint32_t p_channel) const {
	// Cheap smooth pseudo-noise in [-1, 1]: a few unrelated sine waves.
	const double o = double((seed + p_channel * 7919u) % 1000u);
	return Math::sin(p_t * 1.0 + o) * 0.5 + Math::sin(p_t * 1.73 + o * 2.1) * 0.35 + Math::sin(p_t * 2.91 + o * 3.7) * 0.15;
}

void ScreenShakeBase::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PROCESS: {
			if (_is_editor()) {
				set_process(false);
				return;
			}
			const double delta = get_process_delta_time();
			time += delta;
			trauma = MAX(0.0, trauma - decay * delta);
			if (trauma <= 0.0) {
				_reset_shake();
				set_process(false);
				return;
			}
			_apply_shake(_get_shake_amount(), time * frequency);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_reset_shake();
		} break;
	}
}

void ScreenShakeBase::shake(double p_strength) {
	trauma = CLAMP(trauma + p_strength, 0.0, 1.0);
	if (!_is_editor()) {
		set_process(true);
	}
}

void ScreenShakeBase::stop() {
	trauma = 0.0;
	_reset_shake();
	set_process(false);
}

void ScreenShakeBase::set_camera_path(const NodePath &p_path) {
	_reset_shake();
	camera_path = p_path;
}
NodePath ScreenShakeBase::get_camera_path() const {
	return camera_path;
}
void ScreenShakeBase::set_trauma(double p_trauma) {
	trauma = CLAMP(p_trauma, 0.0, 1.0);
	if (trauma > 0.0 && !_is_editor() && is_inside_tree()) {
		set_process(true);
	}
}
double ScreenShakeBase::get_trauma() const {
	return trauma;
}
void ScreenShakeBase::set_decay(double p_decay) {
	decay = MAX(0.0, p_decay);
}
double ScreenShakeBase::get_decay() const {
	return decay;
}
void ScreenShakeBase::set_frequency(double p_frequency) {
	frequency = MAX(0.0, p_frequency);
}
double ScreenShakeBase::get_frequency() const {
	return frequency;
}

void ScreenShakeBase::_bind_methods() {
	ClassDB::bind_method(D_METHOD("shake", "strength"), &ScreenShakeBase::shake, DEFVAL(0.5));
	ClassDB::bind_method(D_METHOD("stop"), &ScreenShakeBase::stop);
	ClassDB::bind_method(D_METHOD("set_camera_path", "path"), &ScreenShakeBase::set_camera_path);
	ClassDB::bind_method(D_METHOD("get_camera_path"), &ScreenShakeBase::get_camera_path);
	ClassDB::bind_method(D_METHOD("set_trauma", "trauma"), &ScreenShakeBase::set_trauma);
	ClassDB::bind_method(D_METHOD("get_trauma"), &ScreenShakeBase::get_trauma);
	ClassDB::bind_method(D_METHOD("set_decay", "decay"), &ScreenShakeBase::set_decay);
	ClassDB::bind_method(D_METHOD("get_decay"), &ScreenShakeBase::get_decay);
	ClassDB::bind_method(D_METHOD("set_frequency", "frequency"), &ScreenShakeBase::set_frequency);
	ClassDB::bind_method(D_METHOD("get_frequency"), &ScreenShakeBase::get_frequency);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "camera_path"), "set_camera_path", "get_camera_path");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "trauma", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_trauma", "get_trauma");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "decay", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_decay", "get_decay");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frequency", PROPERTY_HINT_RANGE, "0,100,0.1,or_greater"), "set_frequency", "get_frequency");
}

ScreenShakeBase::ScreenShakeBase() {
	seed = uint32_t(ObjectID(get_instance_id()).operator uint64_t() & 0xffff);
	set_process(false);
}

/* ScreenShake2D */

Camera2D *ScreenShake2D::_get_camera() const {
	if (!is_inside_tree()) {
		return nullptr;
	}
	if (!camera_path.is_empty()) {
		return Object::cast_to<Camera2D>(get_node_or_null(camera_path));
	}
	return Object::cast_to<Camera2D>(get_parent());
}

void ScreenShake2D::_apply_shake(double p_amount, double p_t) {
	Camera2D *cam = _get_camera();
	if (!cam) {
		return;
	}
	const Vector2 new_offset = Vector2(max_offset.x * p_amount * _noise(p_t, 0), max_offset.y * p_amount * _noise(p_t, 1));
	cam->set_offset(cam->get_offset() - applied_offset + new_offset);
	applied_offset = new_offset;
}

void ScreenShake2D::_reset_shake() {
	Camera2D *cam = _get_camera();
	if (cam) {
		cam->set_offset(cam->get_offset() - applied_offset);
	}
	applied_offset = Vector2();
}

void ScreenShake2D::set_max_offset(const Vector2 &p_offset) {
	max_offset = p_offset;
}
Vector2 ScreenShake2D::get_max_offset() const {
	return max_offset;
}

void ScreenShake2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_max_offset", "offset"), &ScreenShake2D::set_max_offset);
	ClassDB::bind_method(D_METHOD("get_max_offset"), &ScreenShake2D::get_max_offset);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "max_offset", PROPERTY_HINT_NONE, "suffix:px"), "set_max_offset", "get_max_offset");
}

/* ScreenShake3D */

Camera3D *ScreenShake3D::_get_camera() const {
	if (!is_inside_tree()) {
		return nullptr;
	}
	if (!camera_path.is_empty()) {
		return Object::cast_to<Camera3D>(get_node_or_null(camera_path));
	}
	return Object::cast_to<Camera3D>(get_parent());
}

void ScreenShake3D::_apply_shake(double p_amount, double p_t) {
	Camera3D *cam = _get_camera();
	if (!cam) {
		return;
	}
	const Vector2 new_offset = Vector2(max_offset.x * p_amount * _noise(p_t, 0), max_offset.y * p_amount * _noise(p_t, 1));
	cam->set_h_offset(cam->get_h_offset() - applied_offset.x + new_offset.x);
	cam->set_v_offset(cam->get_v_offset() - applied_offset.y + new_offset.y);
	applied_offset = new_offset;
}

void ScreenShake3D::_reset_shake() {
	Camera3D *cam = _get_camera();
	if (cam) {
		cam->set_h_offset(cam->get_h_offset() - applied_offset.x);
		cam->set_v_offset(cam->get_v_offset() - applied_offset.y);
	}
	applied_offset = Vector2();
}

void ScreenShake3D::set_max_offset(const Vector2 &p_offset) {
	max_offset = p_offset;
}
Vector2 ScreenShake3D::get_max_offset() const {
	return max_offset;
}

void ScreenShake3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_max_offset", "offset"), &ScreenShake3D::set_max_offset);
	ClassDB::bind_method(D_METHOD("get_max_offset"), &ScreenShake3D::get_max_offset);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "max_offset", PROPERTY_HINT_NONE, "suffix:m"), "set_max_offset", "get_max_offset");
}
