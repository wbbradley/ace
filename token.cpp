#include "token.h"
#include "dbg.h"
#include "assert.h"
#include <sstream>


bool tkvisible(token_kind tk) {
	switch (tk) {
	default:
		return true;
	case tk_newline:
	case tk_indent:
	case tk_outdent:
		return false;
	}
}

std::string zion_token_t::str() const {
	std::stringstream ss;
	if (text.size() != 0) {
		ss << C_ID << "'" << text << "'" << C_RESET;
		ss << "@";
	}
	ss << location.str();
	return ss.str();
}

#define tk_case(x) case tk_##x: return #x

const char *tkstr(token_kind tk) {
	switch (tk) {
	tk_case(any);
	tk_case(as);
	tk_case(and);
	tk_case(assign);
	tk_case(atom);
	tk_case(becomes);
	tk_case(break);
	tk_case(char);
	tk_case(colon);
	tk_case(comma);
	tk_case(comment);
	tk_case(continue);
	tk_case(def);
	tk_case(divide_by);
	tk_case(divide_by_eq);
	tk_case(dot);
	tk_case(double_dot);
	tk_case(elif);
	tk_case(else);
	tk_case(equal);
	tk_case(error);
	tk_case(float);
	tk_case(for);
	tk_case(raw_float);
	tk_case(gt);
	tk_case(gte);
	tk_case(has);
	tk_case(identifier);
	tk_case(if);
	tk_case(in);
	tk_case(indent);
	tk_case(inequal);
	tk_case(integer);
	tk_case(raw_integer);
	tk_case(is);
	tk_case(lcurly);
	tk_case(link);
	tk_case(lparen);
	tk_case(lsquare);
	tk_case(lt);
	tk_case(lte);
	tk_case(matches);
	tk_case(maybe);
	tk_case(bang);
	tk_case(minus);
	tk_case(minus_eq);
	tk_case(mod);
	tk_case(mod_eq);
	tk_case(module);
	tk_case(newline);
	tk_case(none);
	tk_case(not);
	tk_case(or);
	tk_case(outdent);
	tk_case(pass);
	tk_case(plus);
	tk_case(plus_eq);
	tk_case(maybe_eq);
	tk_case(rcurly);
	tk_case(return);
	tk_case(rparen);
	tk_case(rsquare);
	tk_case(semicolon);
	tk_case(sizeof);
	tk_case(space);
	tk_case(string);
	tk_case(raw_string);
	tk_case(times);
	tk_case(times_eq);
	tk_case(tag);
	tk_case(to);
	tk_case(type);
	tk_case(get_typeid);
	tk_case(var);
	tk_case(version);
	tk_case(while);
	tk_case(when);
	tk_case(with);
	}
	return "";
}

void ensure_space_before(token_kind prior_tk) {
	switch (prior_tk) {
	case tk_none:
	case tk_break:
	case tk_char:
	case tk_colon:
	case tk_comment:
	case tk_continue:
	case tk_dot:
	case tk_double_dot:
	case tk_indent:
	case tk_lcurly:
	case tk_lparen:
	case tk_lsquare:
	case tk_newline:
	case tk_outdent:
	case tk_pass:
	case tk_rcurly:
	case tk_float:
	case tk_raw_float:
	case tk_rparen:
	case tk_rsquare:
	case tk_space:
	case tk_get_typeid:
	case tk_sizeof:
	case tk_maybe:
	case tk_bang:
		break;
	case tk_and:
	case tk_any:
	case tk_as:
	case tk_assign:
	case tk_atom:
	case tk_becomes:
	case tk_comma:
	case tk_def:
	case tk_divide_by:
	case tk_divide_by_eq:
	case tk_elif:
	case tk_else:
	case tk_equal:
	case tk_error:
	case tk_for:
	case tk_gt:
	case tk_gte:
	case tk_has:
	case tk_identifier:
	case tk_if:
	case tk_in:
	case tk_inequal:
	case tk_integer:
	case tk_raw_integer:
	case tk_is:
	case tk_link:
	case tk_lt:
	case tk_lte:
	case tk_matches:
	case tk_maybe_eq:
	case tk_minus:
	case tk_minus_eq:
	case tk_mod:
	case tk_mod_eq:
	case tk_module:
	case tk_not:
	case tk_or:
	case tk_plus:
	case tk_plus_eq:
	case tk_return:
	case tk_semicolon:
	case tk_string:
	case tk_raw_string:
	case tk_tag:
	case tk_times:
	case tk_times_eq:
	case tk_to:
	case tk_type:
	case tk_var:
	case tk_version:
	case tk_when:
	case tk_with:
	case tk_while:
		printf(" ");
		break;
	}
}

char tk_char_to_char(const std::string &token_text) {
	assert(token_text[0] == '\'');
	assert(token_text[token_text.size() - 1] == '\'');
	assert(token_text.size() == 3 || token_text.size() == 4);
	char val = token_text[1];
	if (val == '\\') {
		char val = token_text[2];
		switch (val) {
		case 'b':
			val = '\b';
			break;
		case 'f':
			val = '\f';
			break;
		case 'n':
			val = '\n';
			break;
		case 'r':
			val = '\r';
			break;
		case 't':
			val = '\t';
			break;
		}
	}
	return val;
}

void ensure_indented_line(bool &indented_line, int indent_level) {
	if (!indented_line) {
		indented_line = true;
		for (int i = 0; i < indent_level; ++i) {
			printf("\t");
		}
	}
}

void zion_token_t::emit(int &indent_level, token_kind &last_tk, bool &indented_line) {
	/* Pretty print this token in a stream. */
	if (tkvisible(tk)) {
		ensure_indented_line(indented_line, indent_level);
	}

	switch (tk) {
	case tk_none: break;
	case tk_lparen:
		printf("(");
		break;
	case tk_rparen:
		printf(")");
		break;
	case tk_comma:
		printf(",");
		break;
	case tk_lcurly:
		printf("{");
		break;
	case tk_rcurly:
		printf("}");
		break;
	case tk_lsquare:
		printf("[");
		break;
	case tk_rsquare:
		printf("]");
		break;
	case tk_colon:
		printf(":");
		break;
	case tk_semicolon:
		printf(";");
		break;
	case tk_any:
		printf("any");
		break;
	case tk_as:
		printf("as");
		break;
	case tk_has:
		printf("has");
		break;
	case tk_def:
		printf("def");
		break;
	case tk_tag:
		printf("tag");
		break;
	case tk_sizeof:
		printf("__sizeof__");
		break;
	case tk_get_typeid:
		printf("__get_typeid__");
		break;
	case tk_for:
		printf("for");
		break;
	case tk_if:
		printf("if");
		break;
	case tk_is:
		printf("is");
		break;
	case tk_elif:
		printf("elif");
		break;
	case tk_else:
		printf("else");
		break;
	case tk_while:
		printf("while");
		break;
	case tk_when:
		printf("when");
		break;
	case tk_with:
		printf("with");
		break;
	case tk_matches:
		printf("matches");
		break;
	case tk_module:
		printf("module");
		break;
	case tk_link:
		printf("link");
		break;
	case tk_to:
		printf("to");
		break;
	case tk_break:
		printf("break");
		break;
	case tk_continue:
		printf("continue");
		break;
	case tk_type:
		printf("type");
		break;
	case tk_error:
		printf("ė");
		break;
	case tk_space:
		printf(" ");
		break;
	case tk_becomes:
		printf(":=");
		break;
	case tk_plus_eq:
		printf("+=");
		break;
	case tk_maybe:
		printf("?");
		break;
	case tk_bang:
		printf("!");
		break;
	case tk_maybe_eq:
		printf("?=");
		break;
	case tk_minus_eq:
		printf("-=");
		break;
	case tk_times_eq:
		printf("*=");
		break;
	case tk_divide_by_eq:
		printf("/=");
		break;
	case tk_mod_eq:
		printf("%%=");
		break;
	case tk_newline:
		printf("\n");
		indented_line = false;
		break;
	case tk_indent:
		++indent_level;
		break;
	case tk_outdent:
		assert(indent_level >= 1);
		--indent_level;
		break;
	case tk_identifier:
		ensure_space_before(last_tk);
		printf("%s", text.c_str());
		break;
	case tk_comment:
		assert(false);
		break;
	case tk_atom:
	case tk_char:
	case tk_string:
	case tk_raw_string:
	case tk_integer:
	case tk_raw_integer:
	case tk_float:
	case tk_raw_float:
	case tk_version:
		ensure_space_before(last_tk);
		printf("%s", text.c_str());
		break;
	case tk_pass:
		printf("pass");
		break;
	case tk_not:
		printf("not");
		break;
	case tk_in:
		printf("in");
		break;
	case tk_or:
		printf("or");
		break;
	case tk_and:
		printf("and");
		break;
	case tk_dot:
		printf(".");
		break;
	case tk_double_dot:
		printf("..");
		break;
	case tk_var:
		printf("var");
		break;
	case tk_return:
		printf("return");
		break;
	case tk_equal:
		ensure_space_before(last_tk);
		printf("==");
		break;
	case tk_inequal:
		ensure_space_before(last_tk);
		printf("!=");
		break;
	case tk_lt:
		ensure_space_before(last_tk);
		printf("<");
		break;
	case tk_gt:
		ensure_space_before(last_tk);
		printf(">");
		break;
	case tk_lte:
		ensure_space_before(last_tk);
		printf("<=");
		break;
	case tk_gte:
		ensure_space_before(last_tk);
		printf(">=");
		break;
	case tk_assign:
		ensure_space_before(last_tk);
		printf("=");
		break;
	case tk_plus:
		ensure_space_before(last_tk);
		printf("+");
		break;
	case tk_minus:
		ensure_space_before(last_tk);
		printf("-");
		break;
	case tk_times:
		printf("*");
		break;
	case tk_divide_by:
		ensure_space_before(last_tk);
		printf("/");
		break;
	case tk_mod:
		ensure_space_before(last_tk);
		printf("%%");
		break;
	}
	last_tk = tk;
}

void emit_tokens(const std::vector<zion_token_t> &tokens) {
	int indent_level = 0;
	token_kind tk = tk_none;
	bool indented_line = false;
	for (auto token : tokens) {
		token.emit(indent_level, tk, indented_line);
	}
}
