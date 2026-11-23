/**************************************************************************/
/*  ipde_gameplay_nodes.cpp                                               */
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

#include "ipde_gameplay_nodes.h"

#include "core/config/engine.h"
#include "core/math/random_number_generator.h"
#include "core/object/class_db.h"

static bool _ipde_is_editor() {
	return Engine::get_singleton()->is_editor_hint();
}

/* HealthComponent */

void HealthComponent::_set_health(double p_health) {
	const double old = health;
	health = CLAMP(p_health, 0.0, max_health);
	if (health != old) {
		emit_signal(SNAME("health_changed"), health, old);
	}
	if (health > 0.0) {
		dead = false;
	}
}

void HealthComponent::_check_death() {
	if (health <= 0.0 && !dead && !_ipde_is_editor()) {
		dead = true;
		emit_signal(SNAME("died"));
	}
}

void HealthComponent::_notification(int p_what) {
	if (p_what == NOTIFICATION_PROCESS) {
		invulnerability_left -= get_process_delta_time();
		if (invulnerability_left <= 0.0) {
			invulnerability_left = 0.0;
			set_process(false);
		}
	}
}

double HealthComponent::damage(double p_amount) {
	if (p_amount <= 0.0 || dead || invulnerable || invulnerability_left > 0.0) {
		return 0.0;
	}
	const double before = health;
	_set_health(health - p_amount);
	const double dealt = before - health;
	if (dealt > 0.0) {
		emit_signal(SNAME("damaged"), dealt);
		_check_death();
		if (invulnerability_time > 0.0 && !dead) {
			invulnerability_left = invulnerability_time;
			set_process(true);
		}
	}
	return dealt;
}

double HealthComponent::heal(double p_amount) {
	if (p_amount <= 0.0 || dead) {
		return 0.0;
	}
	const double before = health;
	_set_health(health + p_amount);
	const double healed = health - before;
	if (healed > 0.0) {
		emit_signal(SNAME("healed"), healed);
	}
	return healed;
}

void HealthComponent::kill() {
	if (!dead) {
		_set_health(0.0);
		_check_death();
	}
}

void HealthComponent::revive(double p_health) {
	dead = false;
	invulnerability_left = 0.0;
	_set_health(p_health < 0.0 ? max_health : p_health);
}

bool HealthComponent::is_dead() const {
	return dead;
}

double HealthComponent::get_health_ratio() const {
	return max_health > 0.0 ? health / max_health : 0.0;
}

bool HealthComponent::is_invulnerability_active() const {
	return invulnerable || invulnerability_left > 0.0;
}

void HealthComponent::set_max_health(double p_max) {
	max_health = MAX(0.001, p_max);
	if (health > max_health) {
		_set_health(max_health);
	}
}
double HealthComponent::get_max_health() const {
	return max_health;
}
void HealthComponent::set_health(double p_health) {
	_set_health(p_health);
	_check_death();
}
double HealthComponent::get_health() const {
	return health;
}
void HealthComponent::set_invulnerable(bool p_invulnerable) {
	invulnerable = p_invulnerable;
}
bool HealthComponent::is_invulnerable() const {
	return invulnerable;
}
void HealthComponent::set_invulnerability_time(double p_time) {
	invulnerability_time = MAX(0.0, p_time);
}
double HealthComponent::get_invulnerability_time() const {
	return invulnerability_time;
}

void HealthComponent::_bind_methods() {
	ClassDB::bind_method(D_METHOD("damage", "amount"), &HealthComponent::damage);
	ClassDB::bind_method(D_METHOD("heal", "amount"), &HealthComponent::heal);
	ClassDB::bind_method(D_METHOD("kill"), &HealthComponent::kill);
	ClassDB::bind_method(D_METHOD("revive", "health"), &HealthComponent::revive, DEFVAL(-1.0));
	ClassDB::bind_method(D_METHOD("is_dead"), &HealthComponent::is_dead);
	ClassDB::bind_method(D_METHOD("get_health_ratio"), &HealthComponent::get_health_ratio);
	ClassDB::bind_method(D_METHOD("is_invulnerability_active"), &HealthComponent::is_invulnerability_active);
	ClassDB::bind_method(D_METHOD("set_max_health", "max_health"), &HealthComponent::set_max_health);
	ClassDB::bind_method(D_METHOD("get_max_health"), &HealthComponent::get_max_health);
	ClassDB::bind_method(D_METHOD("set_health", "health"), &HealthComponent::set_health);
	ClassDB::bind_method(D_METHOD("get_health"), &HealthComponent::get_health);
	ClassDB::bind_method(D_METHOD("set_invulnerable", "invulnerable"), &HealthComponent::set_invulnerable);
	ClassDB::bind_method(D_METHOD("is_invulnerable"), &HealthComponent::is_invulnerable);
	ClassDB::bind_method(D_METHOD("set_invulnerability_time", "time"), &HealthComponent::set_invulnerability_time);
	ClassDB::bind_method(D_METHOD("get_invulnerability_time"), &HealthComponent::get_invulnerability_time);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_health", PROPERTY_HINT_RANGE, "0.001,1000,0.1,or_greater"), "set_max_health", "get_max_health");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "health", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater"), "set_health", "get_health");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "invulnerable"), "set_invulnerable", "is_invulnerable");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "invulnerability_time", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_invulnerability_time", "get_invulnerability_time");

	ADD_SIGNAL(MethodInfo("health_changed", PropertyInfo(Variant::FLOAT, "health"), PropertyInfo(Variant::FLOAT, "previous_health")));
	ADD_SIGNAL(MethodInfo("damaged", PropertyInfo(Variant::FLOAT, "amount")));
	ADD_SIGNAL(MethodInfo("healed", PropertyInfo(Variant::FLOAT, "amount")));
	ADD_SIGNAL(MethodInfo("died"));
}

/* Lifetime */

void Lifetime::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (autostart && !_ipde_is_editor()) {
				start();
			}
		} break;
		case NOTIFICATION_PROCESS: {
			if (!running) {
				return;
			}
			time_left -= get_process_delta_time();
			if (time_left <= 0.0) {
				running = false;
				set_process(false);
				emit_signal(SNAME("expired"));
				Node *to_free = (free_parent && get_parent()) ? get_parent() : this;
				to_free->queue_free();
			}
		} break;
	}
}

void Lifetime::start(double p_time) {
	if (_ipde_is_editor()) {
		return;
	}
	if (p_time >= 0.0) {
		time = p_time;
	}
	time_left = time;
	running = true;
	set_process(true);
}

void Lifetime::stop() {
	running = false;
	set_process(false);
}

double Lifetime::get_time_left() const {
	return running ? MAX(0.0, time_left) : 0.0;
}

void Lifetime::set_time(double p_time) {
	time = MAX(0.0, p_time);
}
double Lifetime::get_time() const {
	return time;
}
void Lifetime::set_free_parent(bool p_enabled) {
	free_parent = p_enabled;
}
bool Lifetime::is_freeing_parent() const {
	return free_parent;
}
void Lifetime::set_autostart(bool p_enabled) {
	autostart = p_enabled;
}
bool Lifetime::is_autostart() const {
	return autostart;
}

void Lifetime::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start", "time"), &Lifetime::start, DEFVAL(-1.0));
	ClassDB::bind_method(D_METHOD("stop"), &Lifetime::stop);
	ClassDB::bind_method(D_METHOD("get_time_left"), &Lifetime::get_time_left);
	ClassDB::bind_method(D_METHOD("set_time", "time"), &Lifetime::set_time);
	ClassDB::bind_method(D_METHOD("get_time"), &Lifetime::get_time);
	ClassDB::bind_method(D_METHOD("set_free_parent", "enabled"), &Lifetime::set_free_parent);
	ClassDB::bind_method(D_METHOD("is_freeing_parent"), &Lifetime::is_freeing_parent);
	ClassDB::bind_method(D_METHOD("set_autostart", "enabled"), &Lifetime::set_autostart);
	ClassDB::bind_method(D_METHOD("is_autostart"), &Lifetime::is_autostart);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time", PROPERTY_HINT_RANGE, "0,60,0.01,or_greater,suffix:s"), "set_time", "get_time");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "free_parent"), "set_free_parent", "is_freeing_parent");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autostart"), "set_autostart", "is_autostart");

	ADD_SIGNAL(MethodInfo("expired"));
}

/* Bobbing2D */

void Bobbing2D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			base_position = get_position();
			set_process(!_ipde_is_editor());
		} break;
		case NOTIFICATION_PROCESS: {
			if (!active) {
				return;
			}
			time += get_process_delta_time();
			set_position(base_position + amplitude * Math::sin(Math::TAU * (time * frequency + phase)));
		} break;
	}
}

void Bobbing2D::set_amplitude(const Vector2 &p_amplitude) {
	amplitude = p_amplitude;
}
Vector2 Bobbing2D::get_amplitude() const {
	return amplitude;
}
void Bobbing2D::set_frequency(double p_frequency) {
	frequency = p_frequency;
}
double Bobbing2D::get_frequency() const {
	return frequency;
}
void Bobbing2D::set_phase(double p_phase) {
	phase = p_phase;
}
double Bobbing2D::get_phase() const {
	return phase;
}
void Bobbing2D::set_active(bool p_active) {
	active = p_active;
}
bool Bobbing2D::is_active() const {
	return active;
}
void Bobbing2D::reset_base_position() {
	base_position = get_position();
	time = 0.0;
}

void Bobbing2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_amplitude", "amplitude"), &Bobbing2D::set_amplitude);
	ClassDB::bind_method(D_METHOD("get_amplitude"), &Bobbing2D::get_amplitude);
	ClassDB::bind_method(D_METHOD("set_frequency", "frequency"), &Bobbing2D::set_frequency);
	ClassDB::bind_method(D_METHOD("get_frequency"), &Bobbing2D::get_frequency);
	ClassDB::bind_method(D_METHOD("set_phase", "phase"), &Bobbing2D::set_phase);
	ClassDB::bind_method(D_METHOD("get_phase"), &Bobbing2D::get_phase);
	ClassDB::bind_method(D_METHOD("set_active", "active"), &Bobbing2D::set_active);
	ClassDB::bind_method(D_METHOD("is_active"), &Bobbing2D::is_active);
	ClassDB::bind_method(D_METHOD("reset_base_position"), &Bobbing2D::reset_base_position);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "amplitude", PROPERTY_HINT_NONE, "suffix:px"), "set_amplitude", "get_amplitude");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frequency", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:Hz"), "set_frequency", "get_frequency");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "phase", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_phase", "get_phase");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active"), "set_active", "is_active");
}

/* FollowTarget3D / FollowTarget2D */

static double _follow_weight(double p_smoothing, double p_delta) {
	// Frame-rate independent smoothing.
	return p_smoothing <= 0.0 ? 1.0 : 1.0 - Math::exp(-p_smoothing * p_delta);
}

void FollowTarget3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_physics_process(!_ipde_is_editor());
		} break;
		case NOTIFICATION_PHYSICS_PROCESS: {
			if (!active || target.is_empty()) {
				return;
			}
			Node3D *t = Object::cast_to<Node3D>(get_node_or_null(target));
			if (!t) {
				return;
			}
			const Vector3 goal = t->get_global_position() + offset;
			const Vector3 pos = get_global_position().lerp(goal, _follow_weight(smoothing, get_physics_process_delta_time()));
			set_global_position(pos);
			if (look_at_target && !pos.is_equal_approx(t->get_global_position())) {
				const Vector3 dir = (t->get_global_position() - pos).normalized();
				const Vector3 up = Math::abs(dir.dot(Vector3(0, 1, 0))) > 0.99 ? Vector3(0, 0, 1) : Vector3(0, 1, 0);
				look_at(t->get_global_position(), up);
			}
		} break;
	}
}

void FollowTarget3D::snap_to_target() {
	Node3D *t = Object::cast_to<Node3D>(get_node_or_null(target));
	if (t) {
		set_global_position(t->get_global_position() + offset);
	}
}
void FollowTarget3D::set_target(const NodePath &p_target) {
	target = p_target;
}
NodePath FollowTarget3D::get_target() const {
	return target;
}
void FollowTarget3D::set_offset(const Vector3 &p_offset) {
	offset = p_offset;
}
Vector3 FollowTarget3D::get_offset() const {
	return offset;
}
void FollowTarget3D::set_smoothing(double p_smoothing) {
	smoothing = MAX(0.0, p_smoothing);
}
double FollowTarget3D::get_smoothing() const {
	return smoothing;
}
void FollowTarget3D::set_look_at_target(bool p_enabled) {
	look_at_target = p_enabled;
}
bool FollowTarget3D::is_looking_at_target() const {
	return look_at_target;
}
void FollowTarget3D::set_active(bool p_active) {
	active = p_active;
}
bool FollowTarget3D::is_active() const {
	return active;
}

void FollowTarget3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("snap_to_target"), &FollowTarget3D::snap_to_target);
	ClassDB::bind_method(D_METHOD("set_target", "target"), &FollowTarget3D::set_target);
	ClassDB::bind_method(D_METHOD("get_target"), &FollowTarget3D::get_target);
	ClassDB::bind_method(D_METHOD("set_offset", "offset"), &FollowTarget3D::set_offset);
	ClassDB::bind_method(D_METHOD("get_offset"), &FollowTarget3D::get_offset);
	ClassDB::bind_method(D_METHOD("set_smoothing", "smoothing"), &FollowTarget3D::set_smoothing);
	ClassDB::bind_method(D_METHOD("get_smoothing"), &FollowTarget3D::get_smoothing);
	ClassDB::bind_method(D_METHOD("set_look_at_target", "enabled"), &FollowTarget3D::set_look_at_target);
	ClassDB::bind_method(D_METHOD("is_looking_at_target"), &FollowTarget3D::is_looking_at_target);
	ClassDB::bind_method(D_METHOD("set_active", "active"), &FollowTarget3D::set_active);
	ClassDB::bind_method(D_METHOD("is_active"), &FollowTarget3D::is_active);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "target", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D"), "set_target", "get_target");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "offset", PROPERTY_HINT_NONE, "suffix:m"), "set_offset", "get_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smoothing", PROPERTY_HINT_RANGE, "0,50,0.1,or_greater"), "set_smoothing", "get_smoothing");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "look_at_target"), "set_look_at_target", "is_looking_at_target");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active"), "set_active", "is_active");
}

void FollowTarget2D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_physics_process(!_ipde_is_editor());
		} break;
		case NOTIFICATION_PHYSICS_PROCESS: {
			if (!active || target.is_empty()) {
				return;
			}
			Node2D *t = Object::cast_to<Node2D>(get_node_or_null(target));
			if (!t) {
				return;
			}
			const Vector2 goal = t->get_global_position() + offset;
			set_global_position(get_global_position().lerp(goal, _follow_weight(smoothing, get_physics_process_delta_time())));
		} break;
	}
}

void FollowTarget2D::snap_to_target() {
	Node2D *t = Object::cast_to<Node2D>(get_node_or_null(target));
	if (t) {
		set_global_position(t->get_global_position() + offset);
	}
}
void FollowTarget2D::set_target(const NodePath &p_target) {
	target = p_target;
}
NodePath FollowTarget2D::get_target() const {
	return target;
}
void FollowTarget2D::set_offset(const Vector2 &p_offset) {
	offset = p_offset;
}
Vector2 FollowTarget2D::get_offset() const {
	return offset;
}
void FollowTarget2D::set_smoothing(double p_smoothing) {
	smoothing = MAX(0.0, p_smoothing);
}
double FollowTarget2D::get_smoothing() const {
	return smoothing;
}
void FollowTarget2D::set_active(bool p_active) {
	active = p_active;
}
bool FollowTarget2D::is_active() const {
	return active;
}

void FollowTarget2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("snap_to_target"), &FollowTarget2D::snap_to_target);
	ClassDB::bind_method(D_METHOD("set_target", "target"), &FollowTarget2D::set_target);
	ClassDB::bind_method(D_METHOD("get_target"), &FollowTarget2D::get_target);
	ClassDB::bind_method(D_METHOD("set_offset", "offset"), &FollowTarget2D::set_offset);
	ClassDB::bind_method(D_METHOD("get_offset"), &FollowTarget2D::get_offset);
	ClassDB::bind_method(D_METHOD("set_smoothing", "smoothing"), &FollowTarget2D::set_smoothing);
	ClassDB::bind_method(D_METHOD("get_smoothing"), &FollowTarget2D::get_smoothing);
	ClassDB::bind_method(D_METHOD("set_active", "active"), &FollowTarget2D::set_active);
	ClassDB::bind_method(D_METHOD("is_active"), &FollowTarget2D::is_active);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "target", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D"), "set_target", "get_target");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "offset", PROPERTY_HINT_NONE, "suffix:px"), "set_offset", "get_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smoothing", PROPERTY_HINT_RANGE, "0,50,0.1,or_greater"), "set_smoothing", "get_smoothing");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active"), "set_active", "is_active");
}

/* Spawner2D / Spawner3D */

static Vector2 _random_in_circle(double p_radius) {
	if (p_radius <= 0.0) {
		return Vector2();
	}
	const double angle = Math::randf() * Math::TAU;
	const double dist = Math::sqrt(Math::randf()) * p_radius;
	return Vector2(Math::cos(angle), Math::sin(angle)) * dist;
}

static void _ipde_place_spawned(Node *p_node, Node2D *p_spawner, double p_radius) {
	Node2D *n = Object::cast_to<Node2D>(p_node);
	if (n) {
		n->set_global_position(p_spawner->get_global_position() + _random_in_circle(p_radius));
	}
}

static void _ipde_place_spawned(Node *p_node, Node3D *p_spawner, double p_radius) {
	Node3D *n = Object::cast_to<Node3D>(p_node);
	if (n) {
		const Vector2 r = _random_in_circle(p_radius);
		n->set_global_position(p_spawner->get_global_position() + Vector3(r.x, 0, r.y));
	}
}

#define IPDE_SPAWNER_IMPL(m_class)                                                                                                                          \
	Node *m_class::_get_spawn_parent() {                                                                                                                    \
		if (!spawn_parent.is_empty()) {                                                                                                                     \
			return get_node_or_null(spawn_parent);                                                                                                          \
		}                                                                                                                                                   \
		return get_parent() ? get_parent() : this;                                                                                                          \
	}                                                                                                                                                       \
                                                                                                                                                            \
	void m_class::_notification(int p_what) {                                                                                                               \
		switch (p_what) {                                                                                                                                   \
			case NOTIFICATION_READY: {                                                                                                                      \
				if (autostart && !_ipde_is_editor()) {                                                                                                      \
					start();                                                                                                                                \
				}                                                                                                                                           \
			} break;                                                                                                                                        \
			case NOTIFICATION_PROCESS: {                                                                                                                    \
				if (!running) {                                                                                                                             \
					return;                                                                                                                                 \
				}                                                                                                                                           \
				time_left -= get_process_delta_time();                                                                                                      \
				if (time_left <= 0.0) {                                                                                                                     \
					time_left += MAX(0.01, interval);                                                                                                       \
					spawn();                                                                                                                                \
				}                                                                                                                                           \
			} break;                                                                                                                                        \
		}                                                                                                                                                   \
	}                                                                                                                                                       \
                                                                                                                                                            \
	int m_class::get_alive_count() {                                                                                                                        \
		for (uint32_t i = 0; i < alive.size();) {                                                                                                           \
			Object *obj = ObjectDB::get_instance(alive[i]);                                                                                                 \
			Node *n = Object::cast_to<Node>(obj);                                                                                                           \
			if (!n || n->is_queued_for_deletion()) {                                                                                                        \
				alive.remove_at_unordered(i);                                                                                                               \
			} else {                                                                                                                                        \
				i++;                                                                                                                                        \
			}                                                                                                                                               \
		}                                                                                                                                                   \
		return alive.size();                                                                                                                                \
	}                                                                                                                                                       \
                                                                                                                                                            \
	Node *m_class::spawn() {                                                                                                                                \
		if (_ipde_is_editor() || scene.is_null() || !is_inside_tree()) {                                                                                    \
			return nullptr;                                                                                                                                 \
		}                                                                                                                                                   \
		if (max_alive > 0 && get_alive_count() >= max_alive) {                                                                                              \
			return nullptr;                                                                                                                                 \
		}                                                                                                                                                   \
		Node *parent = _get_spawn_parent();                                                                                                                 \
		ERR_FAIL_NULL_V_MSG(parent, nullptr, "Spawn parent not found.");                                                                                    \
		Node *instance = scene->instantiate();                                                                                                              \
		ERR_FAIL_NULL_V_MSG(instance, nullptr, "Couldn't instantiate the spawner scene.");                                                                  \
		parent->add_child(instance);                                                                                                                        \
		_ipde_place_spawned(instance, this, spawn_radius);                                                                                                  \
		alive.push_back(instance->get_instance_id());                                                                                                       \
		emit_signal(SNAME("spawned"), instance);                                                                                                            \
		return instance;                                                                                                                                    \
	}                                                                                                                                                       \
                                                                                                                                                            \
	void m_class::start() {                                                                                                                                 \
		if (_ipde_is_editor()) {                                                                                                                            \
			return;                                                                                                                                         \
		}                                                                                                                                                   \
		running = true;                                                                                                                                     \
		time_left = MAX(0.01, interval);                                                                                                                    \
		set_process(true);                                                                                                                                  \
	}                                                                                                                                                       \
	void m_class::stop() {                                                                                                                                  \
		running = false;                                                                                                                                    \
		set_process(false);                                                                                                                                 \
	}                                                                                                                                                       \
	bool m_class::is_running() const { return running; }                                                                                                    \
	void m_class::set_scene(const Ref<PackedScene> &p_scene) { scene = p_scene; }                                                                           \
	Ref<PackedScene> m_class::get_scene() const { return scene; }                                                                                           \
	void m_class::set_interval(double p_interval) { interval = MAX(0.01, p_interval); }                                                                     \
	double m_class::get_interval() const { return interval; }                                                                                               \
	void m_class::set_max_alive(int p_max) { max_alive = MAX(0, p_max); }                                                                                   \
	int m_class::get_max_alive() const { return max_alive; }                                                                                                \
	void m_class::set_spawn_radius(double p_radius) { spawn_radius = MAX(0.0, p_radius); }                                                                  \
	double m_class::get_spawn_radius() const { return spawn_radius; }                                                                                       \
	void m_class::set_autostart(bool p_enabled) { autostart = p_enabled; }                                                                                  \
	bool m_class::is_autostart() const { return autostart; }                                                                                                \
	void m_class::set_spawn_parent(const NodePath &p_path) { spawn_parent = p_path; }                                                                       \
	NodePath m_class::get_spawn_parent() const { return spawn_parent; }                                                                                     \
                                                                                                                                                            \
	void m_class::_bind_methods() {                                                                                                                         \
		ClassDB::bind_method(D_METHOD("spawn"), &m_class::spawn);                                                                                           \
		ClassDB::bind_method(D_METHOD("start"), &m_class::start);                                                                                           \
		ClassDB::bind_method(D_METHOD("stop"), &m_class::stop);                                                                                             \
		ClassDB::bind_method(D_METHOD("is_running"), &m_class::is_running);                                                                                 \
		ClassDB::bind_method(D_METHOD("get_alive_count"), &m_class::get_alive_count);                                                                       \
		ClassDB::bind_method(D_METHOD("set_scene", "scene"), &m_class::set_scene);                                                                          \
		ClassDB::bind_method(D_METHOD("get_scene"), &m_class::get_scene);                                                                                   \
		ClassDB::bind_method(D_METHOD("set_interval", "interval"), &m_class::set_interval);                                                                 \
		ClassDB::bind_method(D_METHOD("get_interval"), &m_class::get_interval);                                                                             \
		ClassDB::bind_method(D_METHOD("set_max_alive", "max_alive"), &m_class::set_max_alive);                                                              \
		ClassDB::bind_method(D_METHOD("get_max_alive"), &m_class::get_max_alive);                                                                           \
		ClassDB::bind_method(D_METHOD("set_spawn_radius", "radius"), &m_class::set_spawn_radius);                                                           \
		ClassDB::bind_method(D_METHOD("get_spawn_radius"), &m_class::get_spawn_radius);                                                                     \
		ClassDB::bind_method(D_METHOD("set_autostart", "enabled"), &m_class::set_autostart);                                                                \
		ClassDB::bind_method(D_METHOD("is_autostart"), &m_class::is_autostart);                                                                             \
		ClassDB::bind_method(D_METHOD("set_spawn_parent", "path"), &m_class::set_spawn_parent);                                                             \
		ClassDB::bind_method(D_METHOD("get_spawn_parent"), &m_class::get_spawn_parent);                                                                     \
                                                                                                                                                            \
		ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_scene", "get_scene");                         \
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "interval", PROPERTY_HINT_RANGE, "0.01,60,0.01,or_greater,suffix:s"), "set_interval", "get_interval");    \
		ADD_PROPERTY(PropertyInfo(Variant::INT, "max_alive", PROPERTY_HINT_RANGE, "0,1000,1,or_greater"), "set_max_alive", "get_max_alive");                \
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spawn_radius", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater"), "set_spawn_radius", "get_spawn_radius");  \
		ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autostart"), "set_autostart", "is_autostart");                                                            \
		ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "spawn_parent"), "set_spawn_parent", "get_spawn_parent");                                             \
                                                                                                                                                            \
		ADD_SIGNAL(MethodInfo("spawned", PropertyInfo(Variant::OBJECT, "node", PROPERTY_HINT_RESOURCE_TYPE, "Node")));                                      \
	}

IPDE_SPAWNER_IMPL(Spawner2D)
IPDE_SPAWNER_IMPL(Spawner3D)
