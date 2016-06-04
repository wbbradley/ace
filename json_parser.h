#pragma once
#include <memory>
#include <vector>
#include "json.h"
#include "json_lexer.h"
#include <stdint.h>
#include <stdio.h>
#include <ostream>

const std::string escape_json_quotes(const std::string &str);
void escape_json_quotes(std::ostream &os, const std::string &str);
std::string unescape_json_quotes(const json_string_t &str);
std::string unescape_json_quotes(const std::string &str);
std::string unescape_json_quotes(const char *str, size_t len);
void json_add_int_prop_to_node(jsonp node, const std::string &name, uint64_t value, bool avoid_dupes = true);
void json_add_string_prop_to_node(jsonp node, const std::string &name, std::string value, bool avoid_dupes = true);

class json_parser {
public:
	json_parser(json_lexer &lex);
	~json_parser();

	bool parse(jsonp &value);

private:
	bool parse_property(std::string &unescaped_name, std::shared_ptr<json_property> &value);
	bool parse_object(json_object &obj);
	bool parse_array(json_array &nodes);

	json_lexer &m_lex;
};

bool json_parse(std::istream &is, jsonp &result, bool skip_comment = false,
#ifdef ZION_DEBUG
		FILE *fp_out = stderr
#else
		FILE *fp_out = nullptr
#endif
		);
bool json_parse(const std::string filename, jsonp &result, bool skip_comment = false,
#ifdef ZION_DEBUG
		FILE *fp_out = stderr
#else
		FILE *fp_out = nullptr
#endif
		);

bool json_valid(std::istream &is);
