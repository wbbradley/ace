#include "json.h"
#include <iostream>
#include <iostream>
#include <fstream>
#include "utf8/utf8.h"
#include "utils.h"

const char *jvkstr(json_value_kind jvk) {
	switch (jvk) {
	case jvk_string:
		return "string";
	case jvk_number:
		return "number";
	case jvk_bool:
		return "bool";
	case jvk_null:
		return "null";
	case jvk_object:
		return "object";
	case jvk_array:
		return "array";
	}
	return "";
}

uint32_t hexval(char ch) {
	if (ch >= 'a' && ch <= 'f')
		return 10 + ch - 'a';
	if (ch >= 'A' && ch <= 'F')
		return 10 + ch - 'A';
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	assert(false);
	return 0;
}

#ifdef JSON_ZION_DEBUG
void dump_obj(const json_object &obj, const std::string &ns) {
	for (auto &jprop : obj.jprops) {
		dump_jprop(jprop, ns);
	}
}

void dump_value(const jsonp &value, const std::string &ns, int index) {
#ifdef JSON_ZION_DEBUG
	if (!value) {
		std::cerr << "empty node" << std::endl;
		return;
	}
	std::stringstream ss;
	ss << ns;
	if (index > -1)
		ss << "[" << index << "]";
	std::cerr << ss.str() << " (" << jvkstr(value->jvk());

	if (value->jvk() == jvk_number) {
		std::cerr << " = " << value->int32_value() << " == " << value->double_value() << " == " << value->int64_value() << " == " << value->uint64_value();
	} else if (value->jvk() == jvk_bool) {
		std::cerr << " = " << boolstr(value->bool_value());
	} else if (value->jvk() == jvk_string) {
		std::cerr << " = \"" << value->string_value() << "\"";
	}
	std::cerr << ")\n";

	if (value->jvk() == jvk_array)
		dump_array(value->nodes(), ns);
	if (value->jvk() == jvk_object)
		dump_obj(value->obj(), ss.str());
	fflush(stderr);
#endif
}

void dump_array(const json_array &nodes, const std::string &ns) {
	for (json_array::const_iterator i = nodes.begin(); i != nodes.end(); ++i) {
		dump_value(*i, ns, std::distance(nodes.begin(), i));
	}
}

void dump_jprop(const json_property_ptr &jprop, const std::string &ns) {
	std::stringstream ss;

	if (ns.size() > 0)
		ss << ns << ".";
	ss << jprop->name;
	dump_value(jprop->value, ss.str(), -1 /*index*/);
}
#endif

const std::string json_value::empty_string;
const json_array json_value::empty_nodes;
json_object json_value::empty_obj;

jsonp json_object::find_child(const std::string &name) const {
	for (const auto &jprop : jprops) {
		if (jprop->name == name) {
#ifdef EX_ZION_DEBUG
			log(log_info, "looking for %s - found %s\n", name.c_str(),
					jprop->name.c_str());
#endif
			return jprop->value;
		} else {
#ifdef EX_ZION_DEBUG
			log(log_info, "looking for %s - found %s\n", name.c_str(),
					jprop->name.c_str());
#endif
		}
	}
	return jsonp();
}

void json_object::set_child(const std::string &name, jsonp value, bool avoid_dupes) {
	if (avoid_dupes) {
		/* find a child with this name */
		for (auto &jprop : jprops) {
			if (jprop->name == name) {
				/* found it, now replace its value */
				jprop->value = value;
				return;
			}
		}
	}

	/* child did not exist, create a new child property */
	json_property_ptr prop(new json_property);
	prop->name = name;
	prop->value = value;
	jprops.push_back(prop);
}
