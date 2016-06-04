#pragma once
#include <memory>
#include <vector>
#include <stdint.h>
#include "variant.h"
#include "assert.h"

enum json_value_kind {
	jvk_string = 1,
	jvk_number = 2,
	jvk_bool   = 4,
	jvk_null   = 16,
	jvk_object = 32,
	jvk_array  = 64,
};

const char *jvkstr(json_value_kind jvk);
const std::string strip_json_quotes(const std::string &str);

struct json_object;
struct json_property;
struct json_value;

typedef std::shared_ptr<json_property> json_property_ptr;
typedef std::shared_ptr<json_value> jsonp;
typedef std::vector<jsonp> json_array;
typedef std::vector<json_property_ptr>::const_iterator jprop_iterator;

#ifdef JSON_ZION_DEBUG
void dump_jprop(const json_property_ptr &jprop, const std::string &ns);
void dump_obj(const json_object &obj, const std::string &ns);
void dump_value(const jsonp &value, const std::string &ns, int index);
void dump_array(const json_array &nodes, const std::string &ns);
#else
#define dump_jprop(jprop, ns)
#define dump_obj(obj, ns)
#define dump_value(obj, ns, index)
#define dump_array(nodes, ns)
#endif

struct json_property {
	std::string name;
	jsonp value;
};

struct json_value : public std::enable_shared_from_this<json_value> {
	virtual bool as_variant(runtime::variant_kind vk, runtime::variant &vk_out, bool null_ok) const = 0;
	virtual ~json_value() {}
	virtual json_value_kind jvk() = 0;
	virtual double double_value() {
		return 0;
	}

	virtual int32_t int32_value() {
		return 0;
	}

	virtual uint32_t uint32_value() {
		return 0;
	}

	virtual int64_t int64_value() {
		return 0;
	}

	virtual uint64_t uint64_value() {
		return 0;
	}

	virtual bool bool_value() {
		return false;
	}

	virtual const std::string &string_value() {
		return empty_string;
	}

	virtual void resize(size_t i) {
	}

	virtual const json_array &nodes() {
		return empty_nodes;
	}

	virtual jsonp node(size_t i) {
		return jsonp();
	}

	virtual const json_object &obj() const {
		return empty_obj;
	}

	virtual json_object &obj() {
		return empty_obj;
	}

	virtual void pretty_print(std::ostream &os, size_t indentation, bool continue_on_line) const {
		assert(0);
	}

	virtual void write_to_stream(std::ostream &os) const {
		assert(0);
	}

private:
	static const std::string empty_string;
	static const json_array empty_nodes;
	static json_object empty_obj;
};

struct json_object {
	jsonp find_child(const std::string &name) const;
	void set_child(const std::string &name, jsonp value, bool avoid_dupes);
	std::vector<json_property_ptr> jprops;
};

uint32_t hexval(char ch);
std::string unescape_json_quotes(const std::string &str);
std::string unescape_json_quotes(const char *str, size_t len);
const char *boolstr(bool x);
