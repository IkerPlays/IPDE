/**************************************************************************/
/*  ipde_gameplay_nodes.h                                                 */
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
#include "scene/resources/packed_scene.h"

// Health / damage system for any object.
class HealthComponent : public Node {
	GDCLASS(HealthComponent, Node);

	double max_health = 100.0;
	double health = 100.0;
	bool invulnerable = false;
	double invulnerability_time = 0.0; // Seconds of invulnerability after taking damage.
	double invulnerability_left = 0.0;
	bool dead = false;

	void _set_health(double p_health);
	void _check_death();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	double damage(double p_amount);
	double heal(double p_amount);
	void kill();
	void revive(double p_health = -1.0);
	bool is_dead() const;
	double get_health_ratio() const;
	bool is_invulnerability_active() const;

	void set_max_health(double p_max);
	double get_max_health() const;
	void set_health(double p_health);
	double get_health() const;
	void set_invulnerable(bool p_invulnerable);
	bool is_invulnerable() const;
	void set_invulnerability_time(double p_time);
	double get_invulnerability_time() const;
};

// Frees its parent (or itself) after some time: bullets, effects, debris...
class Lifetime : public Node {
	GDCLASS(Lifetime, Node);

	double time = 5.0;
	bool free_parent = true;
	bool autostart = true;
	double time_left = 0.0;
	bool running = false;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void start(double p_time = -1.0);
	void stop();
	double get_time_left() const;

	void set_time(double p_time);
	double get_time() const;
	void set_free_parent(bool p_enabled);
	bool is_freeing_parent() const;
	void set_autostart(bool p_enabled);
	bool is_autostart() const;
};

class Bobbing2D : public Node2D {
	GDCLASS(Bobbing2D, Node2D);

	Vector2 amplitude = Vector2(0, 8);
	double frequency = 0.5;
	double phase = 0.0;
	bool active = true;
	Vector2 base_position;
	double time = 0.0;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_amplitude(const Vector2 &p_amplitude);
	Vector2 get_amplitude() const;
	void set_frequency(double p_frequency);
	double get_frequency() const;
	void set_phase(double p_phase);
	double get_phase() const;
	void set_active(bool p_active);
	bool is_active() const;
	void reset_base_position();
};

// Smoothly follows another node.
class FollowTarget3D : public Node3D {
	GDCLASS(FollowTarget3D, Node3D);

	NodePath target;
	Vector3 offset;
	double smoothing = 5.0; // 0 = instant.
	bool look_at_target = false;
	bool active = true;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void snap_to_target();
	void set_target(const NodePath &p_target);
	NodePath get_target() const;
	void set_offset(const Vector3 &p_offset);
	Vector3 get_offset() const;
	void set_smoothing(double p_smoothing);
	double get_smoothing() const;
	void set_look_at_target(bool p_enabled);
	bool is_looking_at_target() const;
	void set_active(bool p_active);
	bool is_active() const;
};

class FollowTarget2D : public Node2D {
	GDCLASS(FollowTarget2D, Node2D);

	NodePath target;
	Vector2 offset;
	double smoothing = 5.0; // 0 = instant.
	bool active = true;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void snap_to_target();
	void set_target(const NodePath &p_target);
	NodePath get_target() const;
	void set_offset(const Vector2 &p_offset);
	Vector2 get_offset() const;
	void set_smoothing(double p_smoothing);
	double get_smoothing() const;
	void set_active(bool p_active);
	bool is_active() const;
};

// Spawns instances of a scene periodically.
#define IPDE_SPAWNER_DECL(m_class, m_base)                \
	class m_class : public m_base {                        \
		GDCLASS(m_class, m_base);                          \
                                                           \
		Ref<PackedScene> scene;                            \
		double interval = 2.0;                             \
		int max_alive = 10;                                \
		double spawn_radius = 0.0;                         \
		bool autostart = true;                             \
		NodePath spawn_parent;                             \
		double time_left = 0.0;                            \
		bool running = false;                              \
		LocalVector<ObjectID> alive;                       \
                                                           \
		Node *_get_spawn_parent();                         \
                                                           \
	protected:                                             \
		void _notification(int p_what);                    \
		static void _bind_methods();                       \
                                                           \
	public:                                                \
		Node *spawn();                                     \
		void start();                                      \
		void stop();                                       \
		bool is_running() const;                           \
		int get_alive_count();                             \
                                                           \
		void set_scene(const Ref<PackedScene> &p_scene);   \
		Ref<PackedScene> get_scene() const;                \
		void set_interval(double p_interval);              \
		double get_interval() const;                       \
		void set_max_alive(int p_max);                     \
		int get_max_alive() const;                         \
		void set_spawn_radius(double p_radius);            \
		double get_spawn_radius() const;                   \
		void set_autostart(bool p_enabled);                \
		bool is_autostart() const;                         \
		void set_spawn_parent(const NodePath &p_path);     \
		NodePath get_spawn_parent() const;                 \
	};

IPDE_SPAWNER_DECL(Spawner2D, Node2D)
IPDE_SPAWNER_DECL(Spawner3D, Node3D)
