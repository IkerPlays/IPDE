#pragma once

// Small JSON value with parser and serializer (enough for settings, projects and the GitHub API).

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ipde {

class Json {
public:
	enum Type {
		NIL,
		BOOL,
		NUMBER,
		STRING,
		ARRAY,
		OBJECT,
	};

	Json() = default;
	Json(bool p_value) : type(BOOL), boolean(p_value) {}
	Json(double p_value) : type(NUMBER), number(p_value) {}
	Json(int p_value) : type(NUMBER), number(p_value) {}
	Json(int64_t p_value) : type(NUMBER), number((double)p_value) {}
	Json(const char *p_value) : type(STRING), string(p_value) {}
	Json(const std::string &p_value) : type(STRING), string(p_value) {}

	static Json array() {
		Json j;
		j.type = ARRAY;
		return j;
	}
	static Json object() {
		Json j;
		j.type = OBJECT;
		return j;
	}

	// Returns a NIL value and sets r_error if p_text isn't valid JSON.
	static Json parse(const std::string &p_text, std::string *r_error = nullptr);
	std::string dump(bool p_pretty = true) const;

	Type get_type() const { return type; }
	bool is_null() const { return type == NIL; }
	bool is_array() const { return type == ARRAY; }
	bool is_object() const { return type == OBJECT; }
	bool is_string() const { return type == STRING; }

	bool as_bool(bool p_default = false) const { return type == BOOL ? boolean : p_default; }
	double as_number(double p_default = 0.0) const { return type == NUMBER ? number : p_default; }
	int as_int(int p_default = 0) const { return type == NUMBER ? (int)number : p_default; }
	std::string as_string(const std::string &p_default = std::string()) const { return type == STRING ? string : p_default; }

	// Arrays.
	size_t size() const { return type == ARRAY ? items.size() : (type == OBJECT ? fields.size() : 0); }
	const Json &operator[](size_t p_index) const;
	void push_back(const Json &p_value);
	const std::vector<Json> &get_items() const { return items; }

	// Objects.
	bool has(const std::string &p_key) const;
	const Json &operator[](const std::string &p_key) const;
	Json &operator[](const std::string &p_key); // Creates the key (and turns NIL into an object).
	const std::map<std::string, Json> &get_fields() const { return fields; }

private:
	Type type = NIL;
	bool boolean = false;
	double number = 0.0;
	std::string string;
	std::vector<Json> items;
	std::map<std::string, Json> fields;

	void _dump(std::string &r_out, bool p_pretty, int p_indent) const;
};

} // namespace ipde
