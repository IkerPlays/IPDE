#include "core/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ipde {

static const Json &null_json() {
	static const Json n;
	return n;
}

namespace {

struct Parser {
	const std::string &text;
	size_t pos = 0;
	std::string error;

	explicit Parser(const std::string &p_text) : text(p_text) {}

	void skip_ws() {
		while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
			pos++;
		}
	}

	bool fail(const char *p_msg) {
		if (error.empty()) {
			error = std::string(p_msg) + " at " + std::to_string(pos);
		}
		return false;
	}

	static void append_utf8(std::string &r_out, uint32_t p_cp) {
		if (p_cp < 0x80) {
			r_out += (char)p_cp;
		} else if (p_cp < 0x800) {
			r_out += (char)(0xC0 | (p_cp >> 6));
			r_out += (char)(0x80 | (p_cp & 0x3F));
		} else if (p_cp < 0x10000) {
			r_out += (char)(0xE0 | (p_cp >> 12));
			r_out += (char)(0x80 | ((p_cp >> 6) & 0x3F));
			r_out += (char)(0x80 | (p_cp & 0x3F));
		} else {
			r_out += (char)(0xF0 | (p_cp >> 18));
			r_out += (char)(0x80 | ((p_cp >> 12) & 0x3F));
			r_out += (char)(0x80 | ((p_cp >> 6) & 0x3F));
			r_out += (char)(0x80 | (p_cp & 0x3F));
		}
	}

	bool parse_hex4(uint32_t &r_value) {
		if (pos + 4 > text.size()) {
			return fail("bad unicode escape");
		}
		r_value = 0;
		for (int i = 0; i < 4; i++) {
			const char c = text[pos++];
			r_value <<= 4;
			if (c >= '0' && c <= '9') {
				r_value |= c - '0';
			} else if (c >= 'a' && c <= 'f') {
				r_value |= c - 'a' + 10;
			} else if (c >= 'A' && c <= 'F') {
				r_value |= c - 'A' + 10;
			} else {
				return fail("bad unicode escape");
			}
		}
		return true;
	}

	bool parse_string(std::string &r_out) {
		pos++; // Opening quote.
		while (pos < text.size()) {
			const char c = text[pos++];
			if (c == '"') {
				return true;
			}
			if (c != '\\') {
				r_out += c;
				continue;
			}
			if (pos >= text.size()) {
				break;
			}
			const char e = text[pos++];
			switch (e) {
				case '"': r_out += '"'; break;
				case '\\': r_out += '\\'; break;
				case '/': r_out += '/'; break;
				case 'b': r_out += '\b'; break;
				case 'f': r_out += '\f'; break;
				case 'n': r_out += '\n'; break;
				case 'r': r_out += '\r'; break;
				case 't': r_out += '\t'; break;
				case 'u': {
					uint32_t cp;
					if (!parse_hex4(cp)) {
						return false;
					}
					if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 <= text.size() && text[pos] == '\\' && text[pos + 1] == 'u') {
						pos += 2;
						uint32_t low;
						if (!parse_hex4(low)) {
							return false;
						}
						cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
					}
					append_utf8(r_out, cp);
				} break;
				default:
					return fail("bad escape");
			}
		}
		return fail("unterminated string");
	}

	bool parse_value(Json &r_value, int p_depth) {
		if (p_depth > 200) {
			return fail("too deep");
		}
		skip_ws();
		if (pos >= text.size()) {
			return fail("unexpected end");
		}
		const char c = text[pos];
		if (c == '{') {
			pos++;
			r_value = Json::object();
			skip_ws();
			if (pos < text.size() && text[pos] == '}') {
				pos++;
				return true;
			}
			while (true) {
				skip_ws();
				if (pos >= text.size() || text[pos] != '"') {
					return fail("expected key");
				}
				std::string key;
				if (!parse_string(key)) {
					return false;
				}
				skip_ws();
				if (pos >= text.size() || text[pos] != ':') {
					return fail("expected ':'");
				}
				pos++;
				Json value;
				if (!parse_value(value, p_depth + 1)) {
					return false;
				}
				r_value[key] = value;
				skip_ws();
				if (pos < text.size() && text[pos] == ',') {
					pos++;
					continue;
				}
				if (pos < text.size() && text[pos] == '}') {
					pos++;
					return true;
				}
				return fail("expected ',' or '}'");
			}
		}
		if (c == '[') {
			pos++;
			r_value = Json::array();
			skip_ws();
			if (pos < text.size() && text[pos] == ']') {
				pos++;
				return true;
			}
			while (true) {
				Json value;
				if (!parse_value(value, p_depth + 1)) {
					return false;
				}
				r_value.push_back(value);
				skip_ws();
				if (pos < text.size() && text[pos] == ',') {
					pos++;
					continue;
				}
				if (pos < text.size() && text[pos] == ']') {
					pos++;
					return true;
				}
				return fail("expected ',' or ']'");
			}
		}
		if (c == '"') {
			std::string s;
			if (!parse_string(s)) {
				return false;
			}
			r_value = Json(s);
			return true;
		}
		if (text.compare(pos, 4, "true") == 0) {
			pos += 4;
			r_value = Json(true);
			return true;
		}
		if (text.compare(pos, 5, "false") == 0) {
			pos += 5;
			r_value = Json(false);
			return true;
		}
		if (text.compare(pos, 4, "null") == 0) {
			pos += 4;
			r_value = Json();
			return true;
		}
		if (c == '-' || (c >= '0' && c <= '9')) {
			const char *start = text.c_str() + pos;
			char *end = nullptr;
			const double d = strtod(start, &end);
			if (end == start) {
				return fail("bad number");
			}
			pos += end - start;
			r_value = Json(d);
			return true;
		}
		return fail("unexpected character");
	}
};

} // namespace

Json Json::parse(const std::string &p_text, std::string *r_error) {
	Parser parser(p_text);
	Json value;
	if (!parser.parse_value(value, 0)) {
		if (r_error) {
			*r_error = parser.error;
		}
		return Json();
	}
	return value;
}

const Json &Json::operator[](size_t p_index) const {
	return (type == ARRAY && p_index < items.size()) ? items[p_index] : null_json();
}

void Json::push_back(const Json &p_value) {
	if (type != ARRAY) {
		*this = array();
	}
	items.push_back(p_value);
}

bool Json::has(const std::string &p_key) const {
	return type == OBJECT && fields.count(p_key) > 0;
}

const Json &Json::operator[](const std::string &p_key) const {
	if (type != OBJECT) {
		return null_json();
	}
	auto it = fields.find(p_key);
	return it == fields.end() ? null_json() : it->second;
}

Json &Json::operator[](const std::string &p_key) {
	if (type != OBJECT) {
		*this = object();
	}
	return fields[p_key];
}

static void escape(std::string &r_out, const std::string &p_str) {
	r_out += '"';
	for (unsigned char c : p_str) {
		switch (c) {
			case '"': r_out += "\\\""; break;
			case '\\': r_out += "\\\\"; break;
			case '\n': r_out += "\\n"; break;
			case '\r': r_out += "\\r"; break;
			case '\t': r_out += "\\t"; break;
			default:
				if (c < 0x20) {
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", c);
					r_out += buf;
				} else {
					r_out += (char)c;
				}
		}
	}
	r_out += '"';
}

void Json::_dump(std::string &r_out, bool p_pretty, int p_indent) const {
	const std::string nl = p_pretty ? "\n" : "";
	const std::string ind = p_pretty ? std::string((p_indent + 1) * 2, ' ') : "";
	const std::string ind_close = p_pretty ? std::string(p_indent * 2, ' ') : "";
	switch (type) {
		case NIL: r_out += "null"; break;
		case BOOL: r_out += boolean ? "true" : "false"; break;
		case NUMBER: {
			char buf[64];
			if (std::floor(number) == number && std::fabs(number) < 1e15) {
				snprintf(buf, sizeof(buf), "%lld", (long long)number);
			} else {
				snprintf(buf, sizeof(buf), "%.9g", number);
			}
			r_out += buf;
		} break;
		case STRING: escape(r_out, string); break;
		case ARRAY: {
			if (items.empty()) {
				r_out += "[]";
				break;
			}
			r_out += "[" + nl;
			for (size_t i = 0; i < items.size(); i++) {
				r_out += ind;
				items[i]._dump(r_out, p_pretty, p_indent + 1);
				r_out += (i + 1 < items.size() ? "," : "") + nl;
			}
			r_out += ind_close + "]";
		} break;
		case OBJECT: {
			if (fields.empty()) {
				r_out += "{}";
				break;
			}
			r_out += "{" + nl;
			size_t i = 0;
			for (const auto &kv : fields) {
				r_out += ind;
				escape(r_out, kv.first);
				r_out += p_pretty ? ": " : ":";
				kv.second._dump(r_out, p_pretty, p_indent + 1);
				r_out += (++i < fields.size() ? "," : "") + nl;
			}
			r_out += ind_close + "}";
		} break;
	}
}

std::string Json::dump(bool p_pretty) const {
	std::string out;
	_dump(out, p_pretty, 0);
	return out;
}

} // namespace ipde
