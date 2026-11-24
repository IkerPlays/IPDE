#pragma once

// IPDE 3D math: vectors and 4x4 matrices (column-major, right-handed, Y up, camera looks down -Z).

#include <cmath>

namespace ipde {

constexpr float PI = 3.14159265358979f;
constexpr float DEG2RAD = PI / 180.0f;

struct Vec2 {
	float x = 0, y = 0;
};

struct Vec3 {
	float x = 0, y = 0, z = 0;

	Vec3() = default;
	constexpr Vec3(float p_x, float p_y, float p_z) : x(p_x), y(p_y), z(p_z) {}

	Vec3 operator+(const Vec3 &o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
	Vec3 operator-(const Vec3 &o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
	Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
	Vec3 operator*(const Vec3 &o) const { return Vec3(x * o.x, y * o.y, z * o.z); }
	Vec3 operator/(float s) const { return Vec3(x / s, y / s, z / s); }
	Vec3 operator-() const { return Vec3(-x, -y, -z); }
	Vec3 &operator+=(const Vec3 &o) {
		x += o.x;
		y += o.y;
		z += o.z;
		return *this;
	}
	Vec3 &operator-=(const Vec3 &o) {
		x -= o.x;
		y -= o.y;
		z -= o.z;
		return *this;
	}
	Vec3 &operator*=(float s) {
		x *= s;
		y *= s;
		z *= s;
		return *this;
	}
	float length() const { return std::sqrt(x * x + y * y + z * z); }
	float length_squared() const { return x * x + y * y + z * z; }
	Vec3 normalized() const {
		const float l = length();
		return l > 1e-12f ? *this / l : Vec3(0, 0, 0);
	}
};

inline float dot(const Vec3 &a, const Vec3 &b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3 &a, const Vec3 &b) {
	return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

inline Vec3 lerp(const Vec3 &a, const Vec3 &b, float t) {
	return a + (b - a) * t;
}

struct Vec4 {
	float x = 0, y = 0, z = 0, w = 0;
	Vec4() = default;
	constexpr Vec4(float p_x, float p_y, float p_z, float p_w) : x(p_x), y(p_y), z(p_z), w(p_w) {}
	Vec3 xyz() const { return Vec3(x, y, z); }
};

struct Mat4 {
	float m[16]; // Column-major: m[col * 4 + row].

	Mat4() { *this = identity(); }

	static Mat4 identity() {
		Mat4 r(0);
		r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
		return r;
	}
	static Mat4 translate(const Vec3 &t) {
		Mat4 r = identity();
		r.m[12] = t.x;
		r.m[13] = t.y;
		r.m[14] = t.z;
		return r;
	}
	static Mat4 scale(const Vec3 &s) {
		Mat4 r = identity();
		r.m[0] = s.x;
		r.m[5] = s.y;
		r.m[10] = s.z;
		return r;
	}
	static Mat4 rotate_x(float a) {
		Mat4 r = identity();
		const float c = std::cos(a), s = std::sin(a);
		r.m[5] = c;
		r.m[6] = s;
		r.m[9] = -s;
		r.m[10] = c;
		return r;
	}
	static Mat4 rotate_y(float a) {
		Mat4 r = identity();
		const float c = std::cos(a), s = std::sin(a);
		r.m[0] = c;
		r.m[2] = -s;
		r.m[8] = s;
		r.m[10] = c;
		return r;
	}
	static Mat4 rotate_z(float a) {
		Mat4 r = identity();
		const float c = std::cos(a), s = std::sin(a);
		r.m[0] = c;
		r.m[1] = s;
		r.m[4] = -s;
		r.m[5] = c;
		return r;
	}
	// Euler angles in degrees, applied in Y, X, Z order (yaw, pitch, roll).
	static Mat4 rotate_euler(const Vec3 &deg) {
		return rotate_y(deg.y * DEG2RAD) * rotate_x(deg.x * DEG2RAD) * rotate_z(deg.z * DEG2RAD);
	}
	static Mat4 trs(const Vec3 &t, const Vec3 &rot_deg, const Vec3 &s) {
		return translate(t) * rotate_euler(rot_deg) * scale(s);
	}
	static Mat4 perspective(float fovy_rad, float aspect, float znear, float zfar) {
		Mat4 r(0);
		const float f = 1.0f / std::tan(fovy_rad * 0.5f);
		r.m[0] = f / aspect;
		r.m[5] = f;
		r.m[10] = (zfar + znear) / (znear - zfar);
		r.m[11] = -1.0f;
		r.m[14] = 2.0f * zfar * znear / (znear - zfar);
		return r;
	}
	static Mat4 orthographic(float l, float r_, float b, float t, float n, float f) {
		Mat4 r = identity();
		r.m[0] = 2.0f / (r_ - l);
		r.m[5] = 2.0f / (t - b);
		r.m[10] = -2.0f / (f - n);
		r.m[12] = -(r_ + l) / (r_ - l);
		r.m[13] = -(t + b) / (t - b);
		r.m[14] = -(f + n) / (f - n);
		return r;
	}
	static Mat4 look_at(const Vec3 &eye, const Vec3 &target, const Vec3 &up) {
		const Vec3 f = (target - eye).normalized();
		Vec3 s = cross(f, up).normalized();
		if (s.length_squared() < 1e-8f) {
			s = cross(f, Vec3(0, 0, 1)).normalized();
		}
		const Vec3 u = cross(s, f);
		Mat4 r = identity();
		r.m[0] = s.x;
		r.m[4] = s.y;
		r.m[8] = s.z;
		r.m[1] = u.x;
		r.m[5] = u.y;
		r.m[9] = u.z;
		r.m[2] = -f.x;
		r.m[6] = -f.y;
		r.m[10] = -f.z;
		r.m[12] = -dot(s, eye);
		r.m[13] = -dot(u, eye);
		r.m[14] = dot(f, eye);
		return r;
	}

	Mat4 operator*(const Mat4 &o) const {
		Mat4 r(0);
		for (int c = 0; c < 4; c++) {
			for (int rr = 0; rr < 4; rr++) {
				float sum = 0;
				for (int k = 0; k < 4; k++) {
					sum += m[k * 4 + rr] * o.m[c * 4 + k];
				}
				r.m[c * 4 + rr] = sum;
			}
		}
		return r;
	}
	Vec4 operator*(const Vec4 &v) const {
		return Vec4(m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
				m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
				m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
				m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w);
	}
	Vec3 transform_point(const Vec3 &p) const {
		return Vec3(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
				m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
				m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]);
	}
	Vec3 transform_dir(const Vec3 &d) const {
		return Vec3(m[0] * d.x + m[4] * d.y + m[8] * d.z,
				m[1] * d.x + m[5] * d.y + m[9] * d.z,
				m[2] * d.x + m[6] * d.y + m[10] * d.z);
	}
	Vec3 get_translation() const { return Vec3(m[12], m[13], m[14]); }

	// General 4x4 inverse (cofactor expansion).
	Mat4 inverse() const {
		const float *a = m;
		Mat4 r(0);
		float *inv = r.m;
		inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
		inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
		inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
		inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
		inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
		inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
		inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
		inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
		inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
		inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
		inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
		inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
		inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
		inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
		inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
		inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
		float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
		if (std::fabs(det) < 1e-20f) {
			return identity();
		}
		det = 1.0f / det;
		for (float &v : r.m) {
			v *= det;
		}
		return r;
	}

	// Matrix to transform normals (inverse transpose of the upper 3x3).
	Mat4 normal_matrix() const {
		const Mat4 inv = inverse();
		Mat4 r = identity();
		for (int c = 0; c < 3; c++) {
			for (int rr = 0; rr < 3; rr++) {
				r.m[c * 4 + rr] = inv.m[rr * 4 + c];
			}
		}
		return r;
	}

private:
	explicit Mat4(int) {
		for (float &v : m) {
			v = 0;
		}
	}
};

} // namespace ipde
