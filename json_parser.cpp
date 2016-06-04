#include "zion.h"
#include "dbg.h"
#include "json.h"
#include "json_lexer.h"
#include "json_parser.h"
#include "logger_decls.h"
#include <iostream>
#include "utf8/utf8.h"
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include "mmap_file.h"
#include <sstream>
#include "disk.h"
#include "irawstream.h"


bool json_parse(std::istream &is, jsonp &result, bool skip_comment, FILE *fp_out) {
	json_lexer lexer(is, skip_comment);
	json_parser parser(lexer);
	if (!parser.parse(result)) {
		if (fp_out != nullptr) {
			int pos = int(is.tellg());
			fprintf(fp_out, "json parse error near offset (%d)\n", pos);
		}
		return false;
	}
	return true;
}

bool json_valid(std::istream &is) {
	jsonp result;
	return json_parse(is, result);
}

bool json_parse(const std::string filename, jsonp &result, bool skip_comment, FILE *fp_out) {
	result.reset();

#if 1
	mmap_file_t mmap_file(filename);
	if (!mmap_file.valid()) {
		debug(log(log_info, "unable to create mmap_file_t on %s\n",
					  filename.c_str()));
		return false;
	}

	irawstream irs(mmap_file.addr, mmap_file.len);
#else
	std::ifstream irs;
	irs.open(filename.c_str());
#endif

	if (!irs.good()) {
		debug(log(log_info, "unable to create irawstream on %s\n",
					  filename.c_str()));

		return false;
	}
	json_lexer lexer(irs, skip_comment);
	json_parser parser(lexer);
	if (!parser.parse(result)) {
		if (fp_out != nullptr) {
			size_t pos = size_t(irs.tellg());
			size_t line = 0, col = 0;
			if (get_line_col(filename, pos, line, col))
				fprintf(fp_out, "json parse error at %s:(%d, %d)\n", filename.c_str(), (int)line, (int)col);
			else
				fprintf(fp_out, "json parse error in %s near offset (%d)\n", filename.c_str(), (int)pos);
		}
		return false;
	}
	return true;
}

const std::string escape_json_quotes(const std::string &str) {
	std::stringstream ss;

	escape_json_quotes(ss, str);

	assert(ss.str().size() >= str.size() + 2);
	return ss.str();
}

void escape_json_quotes(std::ostream &ss, const std::string &str) {
	ss << '"';
	const char *pch = str.c_str();
	const char *pch_end = pch + str.size();
	for (;pch != pch_end;++pch) {
		switch (*pch) {
		case '\b':
			ss << "\\b";
			continue;
		case '\f':
			ss << "\\f";
			continue;
		case '\n':
			ss << "\\n";
			continue;
		case '\r':
			ss << "\\r";
			continue;
		case '\t':
			ss << "\\t";
			continue;
		case '\"':
			ss << "\\\"";
			continue;
		}

		if (*pch == '\\')
			ss << '\\';

		ss << *pch;
	}
	ss << '"';
}


std::string unescape_json_quotes(const std::string &str) {
	return unescape_json_quotes(str.c_str(), str.size());
}

std::string unescape_json_quotes(const json_string_t &str) {
	return unescape_json_quotes(str.c_str(), str.size());
}

std::string unescape_json_quotes(const char *str, size_t len) {
	assert(len >= 2);
	assert(str[0] == '\"');
	assert(str[len - 1] == '\"');

	std::string res;
	res.reserve(len);
	bool escaped = false;
	const char *str_end = str + len;
	for (const auto *i = str + 1;
			i != str_end - 1;
			i++) {
		if (escaped) {
			escaped = false;
			switch (*i) {
			case 'b':
				res.push_back((char)'\b');
				continue;
			case 'f':
				res.push_back((char)'\f');
				continue;
			case 'n':
				res.push_back((char)'\n');
				continue;
			case 'r':
				res.push_back((char)'\r');
				continue;
			case 't':
				res.push_back((char)'\t');
				continue;
			case 'u':
				assert(std::distance(i, str_end) >= 5);
				uint16_t ch = 0;
				i++;
				ch += hexval(*i);
				ch <<= 4;
				i++;
				ch += hexval(*i);
				ch <<= 4;
				i++;
				ch += hexval(*i);
				ch <<= 4;
				i++;
				ch += hexval(*i);
				std::string utf8_encoding;
				utf8::utf16to8(&ch, &ch + 1, back_inserter(utf8_encoding));
				res += utf8_encoding;
				continue;
			}
		} else if (*i == '\\') {
			escaped = true;
			continue;
		}

		res.push_back(*i);
	}
	return res;
}

class json_number_value : public json_value {
	std::string m_str_value;
	int64_t m_int_value;
	uint64_t m_uint_value;
	double m_double_value;

public:
	virtual bool as_variant(runtime::variant_kind vk, runtime::variant &vk_out, bool null_ok) const {
		if (vk == vk_int) {
			vk_out = runtime::variant((int64_t)m_int_value);
			return true;
		}
		if (vk == vk_uint) {
			vk_out = runtime::variant((int64_t)m_int_value);
			return true;
		} else if (vk == vk_float) {
			vk_out = runtime::variant(float(m_double_value));
			return true;
		} else if (vk == vk_double) {
			vk_out = runtime::variant(m_double_value);
			return true;
		} else {
			assert(false);
			return false;
		}
	}

	json_number_value(double value) {
		m_int_value = value;
		m_double_value = value;

		std::stringstream ss;
		ss << std::fixed << std::setprecision(3) << value;

		m_str_value = ss.str();
	}

	json_number_value(uint64_t value) {
		m_uint_value = value;
		m_int_value = value;
		m_double_value = value;
	}

	json_number_value(const json_string_t &value) {
		m_int_value = strtoll(value.c_str(), NULL, 10);
		m_uint_value = m_int_value;
		m_double_value = atof(value.c_str());
		if (m_double_value != m_int_value)
			m_str_value = value.str();
	}

	virtual ~json_number_value() {
	}

	virtual json_value_kind jvk() {
		return jvk_number;
	}

	virtual double double_value() {
	   	return m_double_value;
   	}

	virtual int32_t int32_value() {
	   	return (int32_t)m_int_value;
   	}

	virtual uint32_t uint32_value() {
	   	return (uint32_t)m_int_value;
   	}

	virtual int64_t int64_value() {
	   	return m_int_value;
   	}

	virtual uint64_t uint64_value() {
	   	return m_int_value;
   	}

	virtual void pretty_print(std::ostream &os, size_t indentation, bool continue_on_line) const {
		if (!continue_on_line) {
			std::string indent(indentation, '\t');
			os << indent;
		}
		if (m_str_value.size() != 0)
			os << m_str_value;
		else
			os << m_int_value;
	}

	virtual void write_to_stream(std::ostream &os) const {
		if (m_str_value.size() != 0)
			os << m_str_value;
		else
			os << m_int_value;
	}
};

class json_string_value : public json_value {
	const std::string m_value;

public:
	virtual bool as_variant(runtime::variant_kind vk, runtime::variant &vk_out, bool null_ok) const {
		if (vk == vk_str) {
			vk_out = runtime::variant(m_value);
			return true;
		} else {
			assert(false);
			return false;
		}
	}

	json_string_value(const std::string &value) : m_value(unescape_json_quotes(value)) {
	}

	virtual ~json_string_value() {
	}

	virtual json_value_kind jvk() {
		return jvk_string;
	}

	virtual const std::string &string_value() {
		return m_value;
	}

	virtual void pretty_print(std::ostream &os, size_t indentation, bool continue_on_line) const {
		if (!continue_on_line) {
			std::string indent(indentation, '\t');
			os << indent;
		}
		escape_json_quotes(os, m_value);
	}

	virtual void write_to_stream(std::ostream &os) const {
		escape_json_quotes(os, m_value);
	}
};

class json_bool_value : public json_value {
	bool m_value;

public:
	json_bool_value(bool value) : m_value(value) {
	}

	virtual bool as_variant(runtime::variant_kind vk, runtime::variant &vk_out, bool null_ok) const {
		if (vk == vk_bool) {
			vk_out = runtime::variant(m_value);
			return true;
		} else {
			assert(false);
			return false;
		}
	}

	virtual ~json_bool_value() {
	}

	virtual json_value_kind jvk() {
		return jvk_bool;
	}

	virtual bool bool_value() {
		return m_value;
	}

	virtual void pretty_print(std::ostream &os, size_t indentation, bool continue_on_line) const {
		if (!continue_on_line) {
			std::string indent(indentation, '\t');
			os << indent;
		}
		os << boolstr(m_value);
	}

	virtual void write_to_stream(std::ostream &os) const {
		os << boolstr(m_value);
	}
};

class json_null_value : public json_value {
public:
	json_null_value() {
	}

	virtual ~json_null_value() {
	}

	virtual bool as_variant(runtime::variant_kind vk, runtime::variant &vk_out, bool null_ok) const {
		if ((vk == vk_null) || null_ok) {
			vk_out = vt_null;
			return true;
		} else {
			assert(false);
			return false;
		}
	}

	virtual json_value_kind jvk() {
		return jvk_null;
	}

	virtual void pretty_print(std::ostream &os, size_t indentation, bool continue_on_line) const {
		if (!continue_on_line) {
			std::string indent(indentation, '\t');
			os << indent;
		}
		os << "null";
	}

	virtual void write_to_stream(std::ostream &os) const {
		os << "null";
	}
};

class json_array_value : public json_value {
	friend class json_parser;

	std::vector<jsonp> m_nodes;

public:
	json_array_value() {
	}

	virtual ~json_array_value() {
	}

	virtual bool as_variant(runtime::variant_kind vk, runtime::variant &vk_out, bool null_ok) const {
		assert(false);
		return false;
	}

	virtual json_value_kind jvk() {
		return jvk_array;
	}

	virtual double number_value() {
		return m_nodes.size();
	}

	virtual bool bool_value() {
		return m_nodes.size() > 0;
	}

	virtual void resize(size_t i) {
		m_nodes.resize(i);
	}

	virtual const json_array &nodes() {
		return m_nodes;
	}

	virtual jsonp node(size_t i) {
	   	return m_nodes[i];
   	}

	virtual void pretty_print(std::ostream &os, size_t indentation, bool continue_on_line) const {
		std::string indent(indentation, '\t');
		if (indentation != 0)
			os << std::endl << indent;
		os << "[" << std::endl;

		for (size_t i = 0; i < m_nodes.size(); i++) {
			m_nodes[i]->pretty_print(os, indentation + 1, false /*continue_on_line*/);
			os << ",\n";
		}
		os << indent << "]";
	}

	virtual void write_to_stream(std::ostream &os) const {
		os << "[";
		const char *sep = "";
		for (size_t i = 0; i < m_nodes.size(); i++) {
			os << sep;
			m_nodes[i]->write_to_stream(os);
			sep = ",";
		}
		os << "]";
	}
};

class json_object_value : public json_value {
	friend class json_parser;

	json_object m_obj;

public:
	json_object_value() {
	}

	virtual ~json_object_value() {
	}
	virtual bool as_variant(runtime::variant_kind vk, runtime::variant &vk_out, bool null_ok) const {
		assert(false);
		return false;
	}

	virtual json_value_kind jvk() {
		return jvk_object;
	}

	virtual const json_object &obj() const {
		return m_obj;
	}

	virtual json_object &obj() {
		return m_obj;
	}

	virtual void resize(size_t i) {
		assert(i == 0);
		m_obj.jprops.resize(i);
	}

	virtual void pretty_print(std::ostream &os, size_t indentation, bool continue_on_line) const {
		std::string indent(indentation, '\t');
		if (indentation != 0) {
			if (continue_on_line) {
				os << std::endl;
			}
			os << indent;
		}
		os << "{" << std::endl;
		std::string indent_inner(indentation + 1, '\t');
		for (size_t i = 0; i < m_obj.jprops.size(); i++) {
			if ((i != 0) && m_obj.jprops[i]->name.find("//") == 0)
				os << std::endl;
			os << indent_inner;
			escape_json_quotes(os, m_obj.jprops[i]->name);
			os << ":";
			m_obj.jprops[i]->value->pretty_print(os, indentation + 1, true /*continue_on_line*/);
			os << ",\n";
		}
		os << indent << "}";
	}

	virtual void write_to_stream(std::ostream &os) const {
		os << '{';
		const char *sep = "";
		for (size_t i = 0; i < m_obj.jprops.size(); i++) {
			os << sep;
			escape_json_quotes(os, m_obj.jprops[i]->name);
			os << ":";
			m_obj.jprops[i]->value->write_to_stream(os);
			sep = ",";
		}
		os << '}';
	}
};

json_parser::json_parser(json_lexer &lex) : m_lex(lex) {
}

json_parser::~json_parser() {
}

enum json_property_parser_state {
	jpps_seek_colon,
	jpps_type_inference,
	jpps_end
};

bool json_parser::parse_property(std::string &unescaped_name,
								 json_property_ptr &jprop) {
	json_property_parser_state jpps = jpps_seek_colon;

	jprop.reset(new json_property);
	std::swap(jprop->name, unescaped_name);

	while (jpps != jpps_end) {
		if (!m_lex.get_token()) {
			return false;
		}

		json_token_kind jtk = m_lex.current_jtk();
		switch (jpps) {
		case jpps_end:
			return true;
		case jpps_seek_colon:
			switch (jtk) {
			case jtk_whitespace:
				break;
			case jtk_colon:
				jpps = jpps_type_inference;
				break;
			default:
				return false;
			}
			break;
		case jpps_type_inference:
			switch (jtk) {
			case jtk_number:
				jprop->value.reset(new json_number_value(m_lex.current_text()));
				jpps = jpps_end;
				break;
			case jtk_string:
				jprop->value.reset(new json_string_value(m_lex.current_text().str()));
				jpps = jpps_end;
				break;
			case jtk_true:
			case jtk_false:
				jprop->value.reset(new json_bool_value(jtk == jtk_true));
				jpps = jpps_end;
				break;
			case jtk_null:
				jprop->value.reset(new json_null_value());
				jpps = jpps_end;
				break;
			case jtk_lbrace: {
					json_object_value *obj_value = new json_object_value;
					jprop->value.reset(obj_value);
					m_lex.advance();
					if (!parse_object(obj_value->m_obj)) {
						return false;
					}
					jpps = jpps_end;
				}
				break;
			case jtk_lbracket: {
					json_array_value *parray = new json_array_value();
					jprop->value.reset(parray);
					m_lex.advance();
					if (!parse_array(parray->m_nodes)) {
						return false;
					}
					assert(m_lex.current_jtk() == jtk_rbracket);
					jpps = jpps_end;
				}
				break;
			case jtk_whitespace:
				break;
			default:
				std::cerr << "encountered " << jtkstr(jtk) << std::endl;
				return false;
			}
			break;
		}

		if (jpps != jpps_end)
			m_lex.advance();
	}
	return true;
}

enum json_parse_array_state {
	jpas_value,
	jpas_comma_seek
};

bool json_parser::parse_array(json_array &array) {
	json_parse_array_state jpas = jpas_value;

	while (m_lex.get_token()) {
		json_token_kind jtk = m_lex.current_jtk();

		switch (jpas) {
		case jpas_value:
			switch (jtk) {
			case jtk_rbracket:
				return true;
			case jtk_whitespace:
				break;
			case jtk_comma:
				/* error : empty value. */
				return false;
			case jtk_lbrace: {
					m_lex.advance();
					jsonp value;
					json_object_value *pobj = new json_object_value();
					value.reset(pobj);
					if (!parse_object(pobj->m_obj)) {
						return false;
					}
					array.push_back(value);
					jpas = jpas_comma_seek;
				}
				break;
			case jtk_lbracket: {
					/* nested array */
					m_lex.advance();
					jsonp value;
					json_array_value *parray = new json_array_value();
					value.reset(parray);
					if (!parse_array(parray->m_nodes)) {
						return false;
					}
					array.push_back(value);
					jpas = jpas_comma_seek;
				}
				break;

			case jtk_string: {
					jsonp value;
					value.reset(new json_string_value(m_lex.current_text().str()));
					array.push_back(value);
					jpas = jpas_comma_seek;
					break;
				}
			case jtk_number: {
					jsonp value;
					value.reset(new json_number_value(m_lex.current_text()));
					array.push_back(value);
					jpas = jpas_comma_seek;
					break;
				}
			case jtk_true:
			case jtk_false: {
					jsonp value;
					value.reset(new json_bool_value(jtk == jtk_true));
					array.push_back(value);
					jpas = jpas_comma_seek;
					break;
				}
			case jtk_null: {
					jsonp value;
					value.reset(new json_null_value());
					array.push_back(value);
					jpas = jpas_comma_seek;
					break;
				}
			default:
				std::cerr << "encountered " << jtkstr(jtk) << std::endl;
				return false;
			}
			break;
		case jpas_comma_seek:
			switch (jtk) {
			case jtk_whitespace:
				break;
			case jtk_comma:
				jpas = jpas_value;
				break;
			case jtk_rbracket:
				return true;
			default:
				std::cerr << "encountered " << jtkstr(jtk) << std::endl;
				return false;
			}
			break;
		}
		m_lex.advance();
	}

	return false;
}

bool json_parser::parse(jsonp &value) {
	value.reset();

	/* Eat the first lbrace '{' */
	if (!m_lex.get_token())
		return false;

	if (m_lex.current_jtk() == jtk_whitespace) {
		m_lex.advance();
		return parse(value);
	}

	if (m_lex.current_jtk() == jtk_lbracket) {
		json_array_value *parray = new json_array_value();
		value.reset(parray);
		m_lex.advance();
		if (!parse_array(parray->m_nodes)) {
			return false;
		}
	} else if (m_lex.current_jtk() == jtk_lbrace) {
		json_object_value *pobj = new json_object_value();
		value.reset(pobj);
		m_lex.advance();
		if (!parse_object(pobj->m_obj)) {
			return false;
		}
	} else {
		assert(false);
		return false;
	}

	return value != NULL;
}

enum json_obj_parser_state {
	jops_read_property,
	jops_after_property
};

bool json_parser::parse_object(json_object &obj) {
	assert(obj.jprops.size() == 0);

	/* Begin parsing properties. */
	json_obj_parser_state jops = jops_read_property;

	while (m_lex.get_token()) {
		json_token_kind jtk = m_lex.current_jtk();
		switch (jops) {
		case jops_read_property:
			switch (jtk) {
			case jtk_whitespace:
				break;
			case jtk_comma:
				/* error : empty property. */
				return false;
			case jtk_rbrace:
				return true;
			case jtk_string: {
					json_property_ptr jprop;
					std::string unescaped_name(unescape_json_quotes(m_lex.current_text()));
					m_lex.advance();
					if (!parse_property(unescaped_name, jprop)) {
						return false;
					}

					obj.jprops.push_back(jprop);
					jops = jops_after_property;
				}
				break;
			default:
				std::cerr << "encountered " << jtkstr(jtk) << std::endl;
				return false;
			}
			break;
		case jops_after_property:
			switch (jtk) {
			case jtk_whitespace:
				break;
			case jtk_comma:
				jops = jops_read_property;
				break;
			case jtk_rbrace:
				return true;
			default:
#ifdef ZION_DEBUG
				std::cerr << "encountered " << jtkstr(jtk) << std::endl;
#endif
				break;
			}
			break;
		}
		m_lex.advance();
	}

	return false;
}

void json_add_int_prop_to_node(jsonp node,
							   const std::string &name,
							   uint64_t value,
							   bool avoid_dupes) {
	if ((node == NULL) || (node->jvk() != jvk_object))
		return;

	jsonp jprop_value;
	jprop_value.reset(new json_number_value(value));
	node->obj().set_child(name, jprop_value, avoid_dupes);
}

void json_add_string_prop_to_node(jsonp node,
								  const std::string &name,
								  std::string value,
								  bool avoid_dupes) {
	if ((node == NULL) || (node->jvk() != jvk_object))
		return;

	jsonp jprop_value;
	jprop_value.reset(new json_string_value(escape_json_quotes(value)));
	node->obj().set_child(name, jprop_value, avoid_dupes);
}


