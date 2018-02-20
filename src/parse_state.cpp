#include "zion.h"
#include "dbg.h"
#include "parse_state.h"
#include "logger_decls.h"
#include <cstdarg>
#include "builtins.h"
#include "types.h"


parse_state_t::parse_state_t(
		status_t &status,
		std::string filename,
		zion_lexer_t &lexer,
		std::map<std::string, ptr<const types::type_t>> type_macros,
		std::map<std::string, ptr<const types::type_t>> &global_type_macros,
		std::vector<token_t> *comments,
		std::set<token_t> *link_ins) :
	filename(filename),
	lexer(lexer),
	status(status),
	type_macros(type_macros),
	global_type_macros(global_type_macros),
	comments(comments),
	link_ins(link_ins)
{
	advance();
}

bool parse_state_t::advance() {
	debug_lexer(log(log_info, "advanced from %s %s", tkstr(token.tk), token.text.c_str()[0] != '\n' ? token.text.c_str() : ""));
	prior_token = token;
	return lexer.get_token(token, newline, comments);
}

void parse_state_t::warning(const char *format, ...) {
	if (lexer.eof()) {
		status.emit_message(log_info, token.location, "encountered end-of-file");
	}
	va_list args;
	va_start(args, format);
	status.emit_messagev(log_error, token.location, format, args);
	va_end(args);
}

void parse_state_t::error(const char *format, ...) {
	if (lexer.eof()) {
		status.emit_message(log_info, token.location, "encountered end-of-file");
	}
	va_list args;
	va_start(args, format);
	status.emit_messagev(log_error, token.location, format, args);
	va_end(args);
}

void add_default_type_macros(type_macros_t &type_macros) {
	const char *ids[] = {
		/* managed bool is a special case, and should be the only managed type in the default type
		 * macros until type parsing gets much more sophisticated. */
		MANAGED_BOOL,

		BOOL_TYPE,
		INT_TYPE,
		TRUE_TYPE,
		FALSE_TYPE,
		NULL_TYPE,
		VOID_TYPE,
		WCHAR_TYPE,
		FLOAT_TYPE,
		CHAR_TYPE,
		TYPE_OP_IF,
		TYPE_OP_NOT,
		TYPE_OP_IF,
		TYPE_OP_GC,
		TYPE_OP_IS_ZERO,
		TYPE_OP_IS_REF,
		TYPE_OP_IS_TRUE,
		TYPE_OP_IS_FALSE,
		TYPE_OP_IS_POINTER,
		TYPE_OP_IS_FUNCTION,
		TYPE_OP_IS_VOID,
		TYPE_OP_IS_NULL,
		TYPE_OP_IS_MAYBE,
	};

	for (auto id : ids) {
		if (type_macros.find(id) == type_macros.end()) {
			type_macros[id] = type_id(make_iid(id));
		}
	}
}

