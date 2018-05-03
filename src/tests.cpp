#include "logger_decls.h"
#include "logger.h"
#include "dbg.h"
#include <sstream>
#include <iterator>
#include <vector>
#include "lexer.h"
#include "parser.h"
#include <regex>
#include <fstream>
#include "compiler.h"
#include "disk.h"
#include "utils.h"
#include "llvm_test.h"
#include "llvm_utils.h"
#include "unification.h"
#include "type_parser.h"
#include <fcntl.h>
#include <unistd.h>

#define test_assert(x) if (!(x)) { \
	log(log_error, "test_assert " c_error(#x) " failed at " c_line_ref("%s:%d"), __FILE__, __LINE__); \
   	return false; \
} else {}

const char *PASSED_TESTS_FILENAME = "tests-passed";


std::vector<token_kind> get_tks(zion_lexer_t &lexer, bool include_newlines, std::vector<token_t> &comments) {
	std::vector<token_kind> tks;
	token_t token;
	bool newline = false;
	while (lexer.get_token(token, newline, &comments)) {
		if (include_newlines && newline && token.tk != tk_outdent) {
			tks.push_back(tk_newline);
		}
		tks.push_back(token.tk);
	}
	return tks;
}

const char *to_str(token_kind tk) {
	return tkstr(tk);
}

template <typename T>
bool check_tks_match(T &expect, T &result) {
	auto e_iter = expect.begin();
	auto r_iter = result.begin();
	auto e_end = expect.end();
	auto r_end = result.end();

	while (e_iter != e_end && r_iter != r_end) {
		if (*e_iter != *r_iter) {
			log(log_error, "expected %s, but got %s",
					to_str(*e_iter), to_str(*r_iter));
			return false;
		}
		++e_iter;
		++r_iter;
	}

	bool e_at_end = (e_iter == e_end);
	bool r_at_end = (r_iter == r_end);
	if (e_at_end != r_at_end) {
		const char *who_ended = e_at_end ? "expected and end" : "got a premature end";
		log(log_error, "%s from list", who_ended);
		return false;
	}

	return (e_iter == e_end) && (r_iter == r_end);
}

template <typename T>
void log_list(log_level_t level, const char *prefix, T &xs) {
	std::stringstream ss;
	const char *sep = "";
	for (auto x : xs) {
		ss << sep;
		ss << to_str(x);
		sep = ", ";
	}
	log(level, "%s [%s]", prefix, ss.str().c_str());
}

bool check_lexer(std::string text, std::vector<token_kind> expect_tks, bool include_newlines, std::vector<token_t> &comments) {
	std::istringstream iss(text);
	zion_lexer_t lexer("check_lexer", iss);
	std::vector<token_kind> result_tks = get_tks(lexer, include_newlines, comments);
	if (!check_tks_match(expect_tks, result_tks)) {
		log(log_info, "for text: '%s'", text.c_str());
		log_list(log_info, "expected", expect_tks);
		log_list(log_info, "got     ", result_tks);
		return false;
	} 
	return true;
}

struct lexer_test_t {
	std::string text;
	std::vector<token_kind> tks;
};

typedef std::vector<lexer_test_t> lexer_tests;

bool lexer_test_comments(const lexer_tests &tests, std::vector<token_t> &comments, bool include_newlines=false) {
	for (auto &test : tests) {
		if (!check_lexer(test.text, test.tks, include_newlines, comments)) {
			return false;
		}
	}
	return true;
}

bool lexer_test(const lexer_tests &tests, bool include_newlines=false) {
	std::vector<token_t> comments;
	for (auto &test : tests) {
		if (!check_lexer(test.text, test.tks, include_newlines, comments)) {
			return false;
		}
	}
	return true;
}

/*
 * LEXER TESTS
 */
bool test_lex_newlines() {
	lexer_tests tests = {
		{"A\n\t(B\n\n)",
			{tk_identifier, tk_newline,
				tk_indent, tk_lparen, tk_identifier,
				tk_rparen, tk_outdent}},
		{"A\n\t(B\n)",
			{tk_identifier, tk_newline,
				tk_indent, tk_lparen, tk_identifier,
				tk_rparen, tk_outdent}},
		{"A\n\tB = [\n\t\t(C),\n\t\tD,\n\t]\n(1)",
			{tk_identifier, tk_newline,
				tk_indent, tk_identifier, tk_assign, tk_lsquare,
				tk_lparen, tk_identifier, tk_rparen, tk_comma,
				tk_identifier, tk_comma,
				tk_rsquare,
				tk_outdent, tk_lparen, tk_integer, tk_rparen}},
		{"A\n\tB", {tk_identifier, tk_newline, tk_indent, tk_identifier, tk_outdent}},
		{"\nA", {tk_newline, tk_identifier}},
		{"C\n(q)", {tk_identifier, tk_newline, tk_lparen, tk_identifier, tk_rparen}},
		{"A\n\tC\n\tD\n(q)", {tk_identifier, tk_newline, tk_indent, tk_identifier, tk_newline, tk_identifier, tk_outdent, tk_lparen, tk_identifier, tk_rparen}},
	};
	return lexer_test(tests, true /*include_newlines*/);
}

bool test_lex_indents() {
	lexer_tests tests = {
		{"\tfib(n-1)", {tk_indent, tk_identifier, tk_lparen, tk_identifier, tk_minus, tk_integer, tk_rparen, tk_outdent}},
		{"A\n\tB", {tk_identifier, tk_indent, tk_identifier, tk_outdent}},
		{"A\n\tB ", {tk_identifier, tk_indent, tk_identifier, tk_outdent}},
		{"\nA\n\tB ", {tk_identifier, tk_indent, tk_identifier, tk_outdent}},
		{"\n\t\nA", {tk_identifier}},
		{"\nA\n\tB C", {tk_identifier, tk_indent, tk_identifier, tk_identifier, tk_outdent}},
		{"\nA\n\tB\n\tC", {tk_identifier, tk_indent, tk_identifier, tk_identifier, tk_outdent}},
		{"\nA\n\tB\n\n\tC", {tk_identifier, tk_indent, tk_identifier, tk_identifier, tk_outdent}},
		{"A\n\tB\n\t\tC\n\tD", {tk_identifier, tk_indent, tk_identifier, tk_indent, tk_identifier, tk_outdent, tk_identifier, tk_outdent}},
		{"A\n\tB\n\t\tC\n\t\tD\n\tE", {tk_identifier, tk_indent, tk_identifier, tk_indent, tk_identifier, tk_identifier, tk_outdent, tk_identifier, tk_outdent}},
	};
	return lexer_test(tests);
}

bool test_lex_comments() {
	lexer_tests tests = {
		{"# hey", {}},
		{"a # hey", {tk_identifier}},
		{"( # hey )", {tk_lparen}},
	};
	std::vector<token_t> comments;
	if (lexer_test_comments(tests, comments)) {
		if (comments.size() != tests.size()) {
			log(log_error, "failed to find the comments");
			return false;
		} else {
			return true;
		}
	} else {
		return false;
	}
}

bool test_lex_functions() {
	lexer_tests tests = {
		{"def", {tk_identifier}},
		{" def", {tk_identifier}},
		{"def ", {tk_identifier}},
		{"_def", {tk_identifier}},
		{"definitely", {tk_identifier}},
		{"def A", {tk_identifier, tk_identifier}},
		{"def A\n", {tk_identifier, tk_identifier}},
		{"def A\n\tstatement", {tk_identifier, tk_identifier, tk_indent, tk_identifier, tk_outdent}},
		{"def A\n\tstatement\n\tstatement", {tk_identifier, tk_identifier, tk_indent, tk_identifier, tk_identifier, tk_outdent}},
		{"def A\n\tpass", {tk_identifier, tk_identifier, tk_indent, tk_identifier, tk_outdent}},
	};
	return lexer_test(tests);
}

bool test_lex_module_stuff() {
	lexer_tests tests = {
		{"module modules", {tk_identifier, tk_identifier}},
		{"module modules @1.0.2", {tk_identifier, tk_identifier, tk_version}},
		{"get foo", {tk_identifier, tk_identifier}},
	};
	return lexer_test(tests);
}

bool test_lex_operators() {
	lexer_tests tests = {
		{"and", {tk_identifier}},
		{"( ),{};[]:", {tk_lparen, tk_rparen, tk_comma, tk_lcurly, tk_rcurly, tk_semicolon, tk_lsquare, tk_rsquare, tk_colon}},
		{"or", {tk_identifier}},
		{"not", {tk_identifier}},
		{"in", {tk_identifier}},
		{"has", {tk_identifier}},
		{"not in", {tk_identifier, tk_identifier}},
		{">", {tk_gt}},
		{"<", {tk_lt}},
		{">=", {tk_gte}},
		{"<=", {tk_lte}},
		{"<a", {tk_lt, tk_identifier}},
		{">a", {tk_gt, tk_identifier}},
		{"<=a", {tk_lte, tk_identifier}},
		{">=a", {tk_gte, tk_identifier}},
		{"a << b", {tk_identifier, tk_shift_left, tk_identifier}},
		{"a >> b", {tk_identifier, tk_shift_right, tk_identifier}},
		{"^", {tk_hat}},
		{"a|b", {tk_identifier, tk_pipe, tk_identifier}},
	};
	return lexer_test(tests);
}

bool test_lex_dependency_keywords() {
	lexer_tests tests = {
		{"to tote", {tk_identifier, tk_identifier}},
		{"link linker", {tk_identifier, tk_identifier}},
		{"module modules # ignore this", {tk_identifier, tk_identifier}},
	};
	return lexer_test(tests);
}

bool test_lex_literals() {
	lexer_tests tests = {
		{"\"hello world\\n\" 13493839", {tk_string, tk_integer}},
		{"\"\"", {tk_string}},
		{"0", {tk_integer}},
		{"0.0", {tk_float}},
		{"0x3892af0", {tk_integer}},
		{"10", {tk_integer}},
	};
	return lexer_test(tests);
}

bool test_lex_syntax() {
	lexer_tests tests = {
		{"retur not note", {tk_identifier, tk_identifier, tk_identifier}},
		{"return note not", {tk_identifier, tk_identifier, tk_identifier}},
		{"return var = == pass.pass..", {tk_identifier, tk_identifier, tk_assign, tk_equal, tk_identifier, tk_dot, tk_identifier, tk_double_dot}},
		{"not", {tk_identifier}},
		{"null", {tk_identifier}},
		{"while", {tk_identifier}},
		{"if", {tk_identifier}},
		{"when", {tk_identifier}},
		{"with", {tk_identifier}},
		{"typeid", {tk_identifier}},
		{"else", {tk_identifier}},
		{"elif", {tk_identifier}},
		{"break", {tk_identifier}},
		{"breakfast", {tk_identifier}},
		{"continue", {tk_identifier}},
		{"continually", {tk_identifier}},
		{"while true\n\tfoo()", {tk_identifier, tk_identifier, tk_indent, tk_identifier, tk_lparen, tk_rparen, tk_outdent}},
		{"not in", {tk_identifier, tk_identifier}},
		{"true false", {tk_identifier, tk_identifier}},
		{" not", {tk_identifier}},
		{" nothing", {tk_identifier}},
		{" not\n\tnot", {tk_identifier, tk_indent, tk_identifier, tk_outdent}},
		{"? + - * / %", {tk_maybe, tk_plus, tk_minus, tk_times, tk_divide_by, tk_mod}},
		{"+=-=*=/=%=:=?=", {tk_plus_eq, tk_minus_eq, tk_times_eq, tk_divide_by_eq, tk_mod_eq, tk_becomes, tk_maybe_eq}},
	};
	return lexer_test(tests);
}

bool test_lex_floats() {
	lexer_tests tests = {
		{"1.0", {tk_float}},
		{"1.0e1", {tk_float}},
		{"123e12 # whatever this is not here\n", {tk_float}},
		{"-123.29382974284e12", {tk_minus, tk_float}},
		{"h(3.14159265)", {tk_identifier, tk_lparen, tk_float, tk_rparen}},
	};
	return lexer_test(tests);
}

bool test_lex_types() {
	lexer_tests tests = {
		{"type x int", {tk_identifier, tk_identifier, tk_identifier}},
	};
	return lexer_test(tests);
}

const char *test_module_name = "-test-";

bool compare_texts(std::string result, std::string expect) {
	/* skips whitespace at the beginning of both strings */
	auto e_i = expect.begin();
	auto e_end = expect.end();
	auto r_i = result.begin();
	auto r_end = result.end();
	for (;e_i != e_end; ++e_i) {
		if (*e_i != ' ') {
			break;
		}
	}

	for (;r_i != r_end; ++r_i) {
		if (*r_i != ' ') {
			break;
		}
	}
	
	while (e_i != e_end && r_i != r_end && *e_i == *r_i) {
		++e_i;
		++r_i;
	}

	return e_i == e_end && r_i == r_end;
}

bool compare_texts(ast::item_t &result, std::string expect) {
	auto printed_result = result.str();
	return compare_texts(printed_result, expect);
}

bool compare_lispy_results(std::string text, ast::item_t &result, std::string expect) {
	if (compare_texts(result, expect)) {
		return true;
	} else {
		auto printed_result = result.str();
		debug(log(log_info, "based on: \n\n%s\n", text.c_str()));
		debug(log(log_info, "expected: \n\n%s\n", expect.c_str()));
		debug(log(log_info, "result: \n\n%s\n", printed_result.c_str()));
		return false;
	}
}

template <typename T, typename... Args>
bool check_parse(std::string text, std::string filename = test_module_name) {
	auto result = parse_text<T>(text, filename);
	if (!result) {
		debug(log(log_error, "failed to get a parsed result"));
		return false;
	}

	/* make sure we can print back the code without crashing */
	log(log_info, "\n%s", result->str().c_str());
	return true;
}

/*
 * PARSER TESTS
 */
bool test_parse_minimal_module() {
	return check_parse<ast::module_t>("module minimal @0.1.0");
}

bool test_parse_module_one_function() {
	return check_parse<ast::module_t>("module foobar @0.1.0\n\ndef foo()\n\tpass");
}

ptr<ast::binary_operator_t> make_one_plus_two() {
	auto expect = ast::create<ast::binary_operator_t>({{"", 1, 3}, tk_plus, "+"});
	expect->function_name = "__plus__";
	expect->lhs = ast::create<ast::literal_expr_t>({{"", 1, 1}, tk_integer, "1"});
	expect->rhs = ast::create<ast::literal_expr_t>({{"", 1, 5}, tk_integer, "2"});
	return expect;
}

bool test_parse_integer_add() {
	return check_parse<ast::expression_t>("1 + 2");
}

bool test_parse_return_integer_add() {
	return check_parse<ast::expression_t>("1 + \"2\"");
}

bool test_parse_module_function_with_return_plus_expr() {
	return check_parse<ast::module_t>(
			"module foobar @0.1.0\ndef foo()\n\treturn 1 + 2");
}

bool test_parse_math_expression() {
	return check_parse<ast::expression_t>("(1 + 2) * -92323");
}

bool test_parse_array_literal() {
	return check_parse<ast::expression_t>("[0, 1, 2]");
}

bool test_parse_multiple_pluses() {
	return check_parse<ast::expression_t>("1 + 2 + 3");
}

bool test_parse_multiple_minuses() {
	return check_parse<ast::expression_t>("1 - 2 - 3");
}

bool test_parse_multiple_times() {
	return check_parse<ast::expression_t>("0 * 1 * 2 / 3");
}

bool test_parse_multiple_dots() {
	return check_parse<ast::expression_t>("a.b.c.d.e.f");
}

bool test_parse_multiple_logical_ops_1() {
	return check_parse<ast::expression_t>("1 and 2 or 3");
}

bool test_parse_multiple_logical_ops_2() {
	return check_parse<ast::expression_t>("1 or 2 and 3");
}

bool test_parse_multiple_logical_ops_3() {
	return check_parse<ast::expression_t>("1 and 2 and 3 and 4");
}

bool test_parse_multiple_logical_ops_4() {
	return check_parse<ast::expression_t>("1 or 2 or 3 or 4");
}

bool test_parse_mixed_precedences() {
	return check_parse<ast::expression_t>(
			"true and -a.b(false, -1 or 2 + 3 and 3 * 4).zion_rules.sour");
}

bool test_parse_recursive_function_call() {
	return check_parse<ast::module_t>(
		   	"module math @1.0\n"
			"def fib(n int) int\n"
			"\tif n < 2\n"
			"\t\treturn n\n"
			"\treturn fib(n-2) + fib(n-1)",
			"test" /*module*/);
}

bool test_parse_if_else() {
	return check_parse<ast::module_t>(
		   	"module minmax @1.0\n"
			"def min(m int, n int) int\n"
			"\tif n < m\n"
			"\t\treturn n\n"
			"\telif m < n\n"
			"\t\treturn m\n"
			"\telse\n"
			"\t\treturn m\n",
			"test" /*module*/);
}

bool test_parse_single_line_when() {
	return check_parse<ast::module_t>(
			"module _\n"
			"def check() int\n"
			"\twhen x is X\n"
			"\t\treturn 1\n"
			"\treturn 1\n"
			"test" /*module*/);
}

bool test_parse_single_function_call() {
	return check_parse<ast::block_t>(
		   	"\tfib(n-1)",
			"test" /*module*/);
}

bool test_parse_semicolon_line_break() {
	return check_parse<ast::block_t>(
		   	"\tx(n-1);var y int = 7\n",
			"test" /*module*/);
}

bool test_parse_n_minus_one() {
	return check_parse<ast::expression_t>(
		   	"n-1");
}

bool test_parse_prefix_expression_not() {
	return check_parse<ast::expression_t>(
		   	"d != not (b >c and a > b)");
}

bool test_parse_empty_quote() {
	return check_parse<ast::statement_t>("\"\"", "\"\"");
}

bool test_parse_link_extern_module_with_link_as() {
	return check_parse<ast::module_t>(
		   	"module www @1.0.0\n"
			"get http @1.0.0 as http1\n");
}

bool test_parse_link_extern_module() {
	return check_parse<ast::module_t>(
		   	"module www @1.0.0\n"
			"get http @7.0.0\n");
}

bool test_parse_link_extern_function() {
	return check_parse<ast::module_t>(
		   	"module www @1.3.2\n"
			"link def open(filename str, mode str) int\n");
}

enum test_output_source_t {
	tos_program,
	tos_compiler_error,
};

const char *tosstr(test_output_source_t tos) {
	switch (tos) {
	case tos_program:
		return c_error("program");
	case tos_compiler_error:
		return c_error("compiler error");
	}
	panic("unreachable");
	return "";
}

bool check_output_contains(test_output_source_t tos, std::string output, std::string expected, bool use_regex) {
	/* make sure that the string output we search does not contain simple ansi
	 * escape sequences */
	std::string result = clean_ansi_escapes(output);
	trim(result);
	if (use_regex) {
		std::smatch match;
		if (std::regex_search(result, match, std::regex(expected.c_str()))) {
			return true;
		}
	}

	if (output == expected) {
		return true;
	}

	return false;
}

bool expect_output_contains(test_output_source_t tos,
	   	std::string output, std::string expected, bool use_regex) {
	if (check_output_contains(tos, output, expected, use_regex)) {
		return true;
	} else {
		if (verbose()) {
			log(log_error, "output from %s was \n" c_internal("vvvvvvvv") "\n%s" c_internal("^^^^^^^^"),
					tosstr(tos),
					output.c_str());
		}
		log(log_error, "The problem is that we couldn't find \"" c_error("%s") "\" in the output.",
				expected.c_str());
		dbg();
		return false;
	}
}

bool expect_output_lacks(test_output_source_t tos,
	   	std::string output, std::string expected, bool use_regex) {
	if (check_output_contains(tos, output, expected, use_regex)) {
		if (!verbose()) {
			log(log_error, "output from %s was \n" c_internal("vvvvvvvv") "\n%s" c_internal("^^^^^^^^"),
					tosstr(tos),
					output.c_str());
		}
		log(log_error, "The problem is that we found \"" c_error("%s") "\" in the output.",
				expected.c_str());
		return false;
	} else {
		return true;
	}
}

bool get_testable_comments(
		const std::vector<token_t> &comments,
		std::vector<std::string> &error_terms,
	   	std::vector<std::string> &unseen_terms,
		bool &skip_test,
		bool &pass_file) {
	const std::string compile_skip_file = "# test: skip";
	const std::string compile_pass_file = "# test: pass";
	const std::string compile_error_prefix = "# error: ";
	const std::string compile_unseen_prefix = "# unseen: ";

	pass_file = false;
	skip_test = false;
	for (const auto &comment : comments) {
		if (starts_with(comment.text, compile_error_prefix)) {
			error_terms.push_back(comment.text.substr(compile_error_prefix.size()));
		} else if (starts_with(comment.text, compile_unseen_prefix)) {
			unseen_terms.push_back(comment.text.substr(compile_unseen_prefix.size()));
		} else if (starts_with(comment.text, compile_skip_file)) {
			assert(!pass_file);
			skip_test = true;
		} else if (starts_with(comment.text, compile_pass_file)) {
			assert(!skip_test);
			pass_file = true;
		}
	}

	if (error_terms.size() == 0 && unseen_terms.size() == 0) {
		if (!pass_file && !skip_test) {
			log(log_error, "tests must specify error terms, or unseen terms, or pass/skip");
			return false;
		}
	}
	return true;
}

bool _check_compiler_error(compiler_t &compiler, int &skipped) {
	tee_logger tee_log;
	bool parsed = compiler.build_parse_modules();
	std::vector<std::string> error_search_terms, unseen_search_terms;
	bool skip_file = false;
	bool pass_file = false;
	if (!get_testable_comments(compiler.get_comments(), error_search_terms,
				unseen_search_terms, skip_file, pass_file))
	{
		return false;
	}

	std::string program_name = compiler.get_program_name();
	if (skip_file) {
		++skipped;
		log(log_warning, "skipping compiler error tests of " c_error("%s"),
				program_name.c_str());
		return true;
	} else {
		if (parsed && compiler.build_type_check_and_code_gen()) {
			/* if everything looks good so far, be sure to check all the modules in
			 * the program using LLVM's built-in checker */
			for (auto &llvm_module_pair : compiler.llvm_modules) {
				llvm_verify_module(*llvm_module_pair.second);
			}

			if (!pass_file) {
				log(log_error, "compilation of " c_module("%s") c_warn(" succeeded") " but we " c_error("wanted it to fail"),
						program_name.c_str());
				return false;
			} else {
				debug_above(2, log(log_info, "compilation of " c_module("%s") c_good(" succeeded") " which is good",
							program_name.c_str()));
				return true;
			}
		}

		bool checked_something = false;

		for (const auto &search_term : error_search_terms) {
			checked_something = true;
			if (!expect_output_contains(
						tos_compiler_error,
						tee_log.captured_logs_as_string(),
						search_term, true /*use_regex*/)) {
				return false;
			}
		}

		for (const auto &search_term : unseen_search_terms) {
			checked_something = true;
			if (!expect_output_lacks(
						tos_compiler_error,
						tee_log.captured_logs_as_string(),
						search_term, true /*use_regex*/)) {
				return false;
			}
		}

		if (pass_file) {
			log(log_error, "compilation of " c_module("%s") c_warn(" failed") " when " c_error("it should have passed."), program_name.c_str());
			return false;
		}

		if (!checked_something) {
			debug_above(2, log(log_error, "compilation of " c_module("%s") c_warn(" failed") " (which is fine), but " c_error("couldn't find any comment checks."),
						program_name.c_str()));

			return false;
		}

		return true;
	}
}

bool check_compiler_error(std::string module_name, int &skipped) {
	compiler_t compiler(module_name, {".", "lib", "tests"});
	bool result = _check_compiler_error(compiler, skipped);
	if (!result) {
		log(log_warning, c_internal("test failed") " for module " c_module("%s") " ---",
				module_name.c_str());
	}
	return result;
}

bool check_code_gen_emitted(std::string test_module_name, std::string regex_string) {
	tee_logger tee_log;
	compiler_t compiler(test_module_name, {".", "lib", "tests"});

	if (compiler.build_parse_modules()) {
		if (compiler.build_type_check_and_code_gen()) {
			std::string code_gen = compiler.dump_llvm_modules();
			debug_above(8, log(log_info, "code generated -\n%s", code_gen.c_str()));
			std::smatch match;
			if (std::regex_search(code_gen, match, std::regex(regex_string))) {
				return true;
			} else {
				log(log_error, "could not find regex " c_internal("/%s/") " in code gen", regex_string.c_str());
				return false;
			}
		}
	}
	return false;
}

bool test_string_stuff() {
	return !starts_with("abc", "bc")
		   	&& starts_with("abc", "ab")
		   	&& starts_with("abc", "abc")
		   	&& !ends_with("abc", "ab")
		   	&& ends_with("abc", "bc")
		   	&& ends_with("abc", "abc");
}

bool test_utf8() {
	if (utf8_sequence_length(0xe6) != 3) {
		log(log_error, "E6 is a 3-byte utf-8 sequence");
		return false;
	} else if (utf8_sequence_length(0xe5) != 3) {
		log(log_error, "E5 is a 3-byte utf-8 sequence");
		return false;
	} else {
		return true;
	}
}

struct test_env : public env_t {
	test_env(env_map_t env_map) : env_map(env_map) {}
	env_map_t env_map;

	virtual ~test_env() {}
	virtual types::type_t::ref get_type(const std::string &name, bool allow_structural_types) const {
		auto iter = env_map.find(name);
		return ((iter != env_map.end() && (!iter->second.first /*structural*/ || allow_structural_types))
				? iter->second.second
				: nullptr);
	}
};

using test_func = std::function<bool ()>;

struct test_desc {
	std::string name;
	test_func func;
};

#define T(x) {#x, x}

auto test_descs = std::vector<test_desc>{
	T(test_llvm_builder),

	{
		"test_string_format",
		[] () -> bool {
			auto a = string_format("1 %d 3 %s", 2, std::string("four").c_str());
			auto b = string_format("1 %f 3 %s", 2.0f, std::string("four").c_str());
			log(log_info, "a = %s", a.c_str());
			log(log_info, "b = %s", b.c_str());
			return (a == "1 2 3 four" &&
				   	b == "1 2.000000 3 four");
		}
	},
	{
		"test_tee_logger",
		[] () -> bool {
			tee_logger tee_log;
			log(log_info, "So test. Much tee. Wow. %s %d", _magenta("Doge"), 100);
			return tee_log.captured_logs_as_string().find(_magenta("Doge")) != std::string::npos;
		}
	},
	{
		"test_tee_logger_flush",
		[] () -> bool {
			tee_logger tee_log_outer;
			log(log_info, "So test. Much tee. Wow. %s %d", _magenta("Doge"), 100);
			if (true) {
				tee_logger tee_log_nested;

				log(log_info, "This is nested. %s %d", _magenta("Doge"), 200);

				if (tee_log_outer.captured_logs_as_string().find("nested") == std::string::npos) {
					log(log_error, "Nested tee_logger captured text");
					return false;
				}
			}

			return true;
		}
	},
	{
		"test_compiler_build_state",
		[] () -> bool {
			auto filename = "xyz.zion";
			token_t token({filename, 1, 1}, tk_identifier, "module");
			auto module = ast::create<ast::module_t>(token, filename);
			return !!module;
		}
	},
	{
		"test_atoms",
		[] () -> bool {
			test_assert(std::string{"a"} == std::string{"a"});
			test_assert(std::string{"bog"} == std::string{"bog"});
			test_assert(!(std::string{"a"} == std::string{"A"}));

			return true;
		}
	},

	{
		"test_check_output_contains",
		[] () -> bool {
			return check_output_contains(
					tos_compiler_error,
				   	"aks\t " c_good("djf") " hadssdkf street askfjdaskdjf",
					R"(street)", true /*use_regex*/);
		}
	},

	{
		"test_expect_output_lacks",
		[] () -> bool {
			return expect_output_lacks(
					tos_compiler_error,
				   	"aks\t " c_good("djf") " hadssdkf street askfjdaskdjf",
					R"(funky chicken)", true /*use_regex*/);
		}
	},

	{
    	"test_base26",
		[] () -> bool {
			int i = -1;
			log(log_info, "base 26 of %d is %s", i, base26(i).c_str());
			return true;
		}
	},

	T(test_string_stuff),
	T(test_utf8),

	T(test_lex_comments),
	T(test_lex_dependency_keywords),
	T(test_lex_functions),
	T(test_lex_indents),
	T(test_lex_literals),
	T(test_lex_module_stuff),
	T(test_lex_newlines),
	T(test_lex_operators),
	T(test_lex_syntax),
	T(test_lex_floats),
	T(test_lex_types),

	{
		"test_type_algebra",
		[] () -> bool {
			auto a1 = type_id(make_iid("int"));
			return true;
		}
	},

	T(test_parse_empty_quote),
	T(test_parse_if_else),
	T(test_parse_integer_add),
	T(test_parse_link_extern_function),
	T(test_parse_link_extern_module),
	T(test_parse_link_extern_module_with_link_as),
	T(test_parse_math_expression),
	T(test_parse_array_literal),
	T(test_parse_minimal_module),
	T(test_parse_mixed_precedences),
	T(test_parse_module_function_with_return_plus_expr),
	T(test_parse_module_one_function),
	T(test_parse_multiple_dots),
	T(test_parse_multiple_logical_ops_1),
	T(test_parse_multiple_logical_ops_2),
	T(test_parse_multiple_logical_ops_3),
	T(test_parse_multiple_logical_ops_4),
	T(test_parse_multiple_minuses),
	T(test_parse_multiple_pluses),
	T(test_parse_multiple_times),
	T(test_parse_n_minus_one),
	T(test_parse_prefix_expression_not),
	T(test_parse_recursive_function_call),
	T(test_parse_single_function_call),
	T(test_parse_semicolon_line_break),
	{
		"test_parse_types",
		[] () -> bool {
			identifier::set generics = {make_iid("T"), make_iid("Q")};
			auto module_id = make_iid("M");

			struct spec {
				std::string first;
				std::string second;
			};

			auto parses = std::vector<spec>{{
				{"bool", "bool"},
				{"int", "int"},
				{"(int)", "int"},
				{"float", "float"},
				{"char", "char"},
				{"*char", "*char"},
				{"*?char", "*?char"},
				{"integer(8, true)", "int8"},
				{"integer(16, false)", "uint16"},
				{"any a", "any a"},
				{"any", "any __1"},
				/* parsing type variables has monotonically increasing side effects */
				{"any", "any __1"},
				{"void", "void"},
				{"map int int", "M.map int int"},
				{"map any b any c", "M.map any b any c"},
				{"T", "any T"},
				{"T char Q", "any T char any Q"},
				{"map (T int) Q", "M.map (any T int) any Q"},
			}};

			for (auto p : parses) {
				reset_generics();
				log("parsing type expression " c_type("%s"), p.first.c_str());
				auto parsed_type = parse_type_expr(p.first, generics, module_id);
				env_map_t env_map;

				auto repr = parsed_type->eval(make_ptr<test_env>(env_map))->repr();
				if (repr != p.second) {
					log(log_error, c_error(" => ") c_type("%s"), repr.c_str());
					log(log_error, c_type("%s") " parsed to " c_type("%s")
							" - should have been " c_type("%s"),
							p.first.c_str(),
							repr.c_str(),
							p.second.c_str());
					return false;
				} else {
					log(" => " c_type("%s"), repr.c_str());
				}

#if 0
				auto evaled = parsed_type->eval(
						types::type_t::map{},
						types::type_t::map{});

				if (evaled->repr() != p.second) {
					log(log_error, c_type("%s") " evaled to " c_type("%s")
							" - should have been " c_type("%s"),
							parsed_type->str().c_str(),
							evaled->str().c_str(),
							p.second.c_str());
					return false;
				}
#endif
			}
			return true;
		}
	},
	{
		"test_parse_pointer_types",
		[] () -> bool {
			auto module_id = make_iid("M");
			env_map_t env_map;

			auto type = parse_type_expr("*?void", {}, module_id)->eval(make_ptr<test_env>(env_map));
			log("type repr is %s", type->str().c_str());
			if (auto maybe = dyncast<const types::type_maybe_t>(type)) {
				if (auto pointer = dyncast<const types::type_ptr_t>(maybe->just)) {
					return dyncast<const types::type_id_t>(pointer->element_type) != nullptr;
				}
			}
			return false;
		}
    },
	{
		"test_unification",
		[] () -> bool {
			identifier::set generics = {make_iid("Container"), make_iid("T")};

			env_map_t env;
			env["int"] = {false, type_integer(
							type_literal({INTERNAL_LOC(), tk_integer, ZION_BITSIZE_STR}),
							type_id(make_iid("true" /*signed*/)))};

			auto unifies = std::vector<types::type_t::pair>{
				make_type_pair("any", "float", generics),
				make_type_pair("void", "void", generics),
				make_type_pair("any a", "int", generics),
				make_type_pair("any", "map int int", generics),
				make_type_pair("any a", "map int str", generics),
				make_type_pair("{int: char}", "{int: char}", generics),
				make_type_pair("{int: any A}", "{any A: int}", generics),
				make_type_pair("{int: any B}", "{any A: Flamethrower}", generics),
				make_type_pair("map any a any b", "map int str", generics),
				make_type_pair("map any a any", "map int str", generics),
				make_type_pair("{any: any b}", "map.Map int str", generics),
				make_type_pair("{any: any}", "map.Map int str", generics),
				make_type_pair("Container any any", "(any look ka) (py py)", generics),
				make_type_pair("map.Map (any) T", "{int: str}", generics),
				make_type_pair("Container int T", "(map int) str", generics),
				make_type_pair("Container T T", "map int int", generics),
				make_type_pair("Container T?", "Foo Bar?", generics),
				make_type_pair("(Container T)?", "(Foo Bar)?", generics),
				make_type_pair("Container T", "[int]", generics),
				make_type_pair("T", "def (x int) float", generics),
				make_type_pair("def _(p T) float", "def _(x int) float", generics),
				make_type_pair("*void", "*int", generics),
				{type_maybe(type_ptr(type_managed(type_struct({}, {}))), {}), type_null()},
				{type_ptr(type_id(make_iid("void"))), type_ptr(type_id(make_iid("X")))},
			};

			auto fails = std::vector<types::type_t::pair>({
					{type_ptr(type_id(make_iid("X"))),type_ptr(type_id(make_iid("void")))},
					// {type_ptr(type_managed(type_struct({}, {}))), type_null()},
					// {type_null(), type_maybe(type_ptr(type_managed(type_struct({}, {}))))},
					make_type_pair("int", "void", {}),
					make_type_pair("map Float", "map float", generics),
					make_type_pair("map float", "map Float", generics),
					make_type_pair("int", "void", generics),
					make_type_pair("(T, T)", "(void, int)", generics),
					{type_ptr(type_id(make_iid("void"))), type_id(make_iid("X"))},
					make_type_pair("int", "map int int", generics),
					make_type_pair("{any a: any a}", "{int: str}", generics),
					make_type_pair("Container float", "[int]", generics),
					make_type_pair("Container T?", "(Foo Bar)?", generics),
					make_type_pair("def (p T) T", "def (x int) float", generics),
					// {type_ptr(type_id(make_iid("void"))), type_ptr(type_managed(type_struct({}, {})))},
					});

			auto _env = make_ptr<test_env>(env);
			for (auto &pair : unifies) {
				if (!unify(pair.first, pair.second, _env, {}).result) {
					log(log_error, "unable to unify %s with %s", pair.first->str().c_str(), pair.second->str().c_str());
					return false;
				}
			}

			for (auto &pair : fails) {
				auto unification = unify(pair.first, pair.second, _env, {});
				if (unification.result) {
					log(log_error, "should have failed unifying %s and %s [%s]",
							pair.first->str().c_str(),
							pair.second->str().c_str(),
							unification.str().c_str());
				}

				test_assert(!unification.result);
			}

			return true;
		}
	},

	{
		"test_type_evaluation",
		[] () -> bool {
			auto module_id = make_iid(GLOBAL_SCOPE_NAME);
			env_map_t env_map;
			env_map["int"] = {false, type_integer(
							type_literal({INTERNAL_LOC(), tk_integer, ZION_BITSIZE_STR}),
							type_id(make_iid("true" /*signed*/)))};
			env_map["Managed"] = {true, type_ptr(type_managed(type_struct({}, {})))};
			env_map["Native"] = {true, type_ptr(type_struct({}, {}))};

			std::string tests[] = {
				"OK",
				"if true OK BAD",
				"if false BAD OK",
				"if (not true) BAD OK",
				"if (not false) OK BAD",
				"if (gc Managed) OK BAD",
				"if (gc Native) BAD OK",
				"if (not (gc Managed)) BAD OK",
				"if (not (gc Native)) OK BAD",
			};
			auto _env = make_ptr<test_env>(env_map);
			for (auto test : tests) {
				auto should_be_x = parse_type_expr(test, {}, module_id);
				log("parsing type expression %s => %s",
						test.c_str(), should_be_x->str().c_str());
				auto evaled = should_be_x->eval(_env);
				log(log_info, "%s evaled to %s",
						should_be_x->str().c_str(),
						evaled->str().c_str());
				if (!types::is_type_id(evaled, "OK", _env)) {
					log(log_error, "failed to get OK from \"%s\" = %s",
							test.c_str(),
							should_be_x->str().c_str());
					return false;
				}
			}
			return true;
		}
	},
	{
		"test_code_gen_module_exists",
		[] () -> bool {
			tee_logger tee_log;
			auto test_module_name = "test_puts_emit";
			compiler_t compiler(test_module_name, {".", "lib", "tests"});

			if (compiler.build_parse_modules()) {
				if (compiler.build_type_check_and_code_gen()) {
					assert(compiler.get_program_scope() != nullptr);
					if (!compiler.get_program_scope()->lookup_module(test_module_name)) {
						log(log_error, "no module %s found", test_module_name);
						return false;
					} else {
						return true;
					}
				}
			}

			return false;
		}
	},

	{
		"test_code_gen_renders",
		[] () -> bool {
			return check_code_gen_emitted("test_puts_emit", "test_puts_emit");
		}
	},

	{
		"test_code_gen_renders_function",
		[] () -> bool {
			return check_code_gen_emitted("test_puts_emit", "declare i32 @puts");
		}
	},

	{
		"test_code_gen_renders_entry",
		[] () -> bool {
			return check_code_gen_emitted("test_puts_emit", "entry:");
		}
	},

};

bool check_filters(std::string name, std::string filter, std::vector<std::string> excludes) {
	if (filter.size() != 0 && name.find(filter.c_str()) == std::string::npos) {
        /* filters are matching any part */
		return false;
	}
	for (auto exclude : excludes) {
        /* excludes are whole-match */
		if (name == exclude) {
			return false;
		}
	}
	return true;
}

void append_excludes(std::string name) {
    int fd = open(PASSED_TESTS_FILENAME, O_WRONLY|O_CREAT|O_APPEND, 0666);
    if (fd > 0) {
        lseek(fd, SEEK_END, 0);
        write(fd, name.c_str(), name.size());
        write(fd, "\n", 1);
        close(fd);
    } else {
        assert(false);
    }
}

std::vector<std::string> read_test_excludes() {
    return readlines(PASSED_TESTS_FILENAME);
}

void truncate_excludes() {
    unlink(PASSED_TESTS_FILENAME);
}

bool run_tests(std::string filter, std::vector<std::string> excludes) {
	int pass=0, total=0, skipped=0;

	if (getenv("DEBUG") == nullptr) {
		setenv("DEBUG", "1", true /*overwrite*/);
	}

	static bool init_from_files = false;
	if (!init_from_files) {
		init_from_files = true;
		std::vector<std::string> leaf_names;
		std::string tests_errors_dir = "tests";
		auto ext_regex = R"(.+\.zion$)";
		if (list_files(tests_errors_dir, ext_regex, leaf_names)) {
			for (auto leaf_name : leaf_names) {
				auto name = leaf_name;
				assert(regex_exists(name, ext_regex));
				name.resize(name.size() - strlen(".zion"));

				if (starts_with(name, "test_")) {
					/* create a test_desc of this file */
					test_desc test_desc = {
						name,
						[tests_errors_dir, name, &skipped] () {
							auto filename = tests_errors_dir + "/" + name;
							note_logger note_logger(string_format(c_warn("testing ") C_FILENAME "%s " C_RESET "...",
										filename.c_str()));
							return check_compiler_error(filename, skipped);
						}
					};

					test_descs.push_back(test_desc);
				}
			}
			debug_above(2, log(log_info, "found %d .zion test files in tests/errors", leaf_names.size()));
		} else {
			panic("can't find any tests/errors files");
			return false;
		}
	}

	/* run all of the compiler test suite */
	bool success = true;
	std::vector<std::string> failures;
	for (auto &test_desc : test_descs) {
		++total;
		if (check_filters(test_desc.name, filter, excludes)) {
			debug_above(2, log(log_info, "------ " c_test_msg("running %s") " ------", test_desc.name.c_str()));

			bool test_failure = true;
			try {
				test_failure = !test_desc.func();
			} catch (user_error &e) {
				print_exception(e);
			}

			if (test_failure) {
				debug_above(2, log(log_error, "------ " c_error("✗ ") c_test_msg("%s") c_error(" FAILED ") "------", test_desc.name.c_str()));
				success = false;
				failures.push_back(test_desc.name);
				break;
			} else {
				debug_above(2, log(log_info, "------ " c_good("✓ ") c_test_msg("%s") c_good(" PASS ") "------", test_desc.name.c_str()));
				append_excludes(test_desc.name);
				++pass;
			}
		} else {
			debug_above(10, log(log_warning, "------ " c_test_msg("skipping %s") " ------", test_desc.name.c_str()));
			++skipped;
		}
	}
	if (skipped) {
		log(log_warning, c_warn("%d TESTS SKIPPED"), skipped);
	}
	if (success) {
		if (pass != 0) {
			log(log_info, c_good("====== %d TESTS PASSED ======"), pass);
		} else {
			log(log_warning, c_warn("====== NO TESTS WERE RUN ======"), pass);
		}
	} else {
		log(log_error, "====== %d/%d TESTS PASSED (" c_error("%d failures") ", " c_warn("%d skipped") ") ======",
				pass, total, total - pass, skipped);
		for (auto fail : failures) {
			log(log_error, "%s failed", fail.c_str());
		}
	}
	return success;
}
