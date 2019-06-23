#include "parser.h"

#include <csignal>
#include <iostream>
#include <stdlib.h>
#include <string>

#include "ast.h"
#include "compiler.h"
#include "disk.h"
#include "host.h"
#include "logger.h"
#include "logger_decls.h"
#include "parse_state.h"
#include "token.h"

using namespace bitter;

class RawParseMode {
public:
  RawParseMode() = delete;
  RawParseMode(RawParseMode &) = delete;
  RawParseMode(parse_state_t &ps)
      : prior_sugar_literals(ps.sugar_literals), ps(ps) {
    ps.sugar_literals = false;
  }
  ~RawParseMode() {
    ps.sugar_literals = prior_sugar_literals;
  }

private:
  bool prior_sugar_literals;
  parse_state_t &ps;
};

identifier_t make_accessor_id(identifier_t id) {
  return identifier_t{"__get_" + id.name, id.location};
}

bool token_begins_type(const Token &token) {
  switch (token.tk) {
  case tk_integer:
  case tk_string:
  case tk_times:
  case tk_lsquare:
  case tk_lparen:
  case tk_identifier:
    return true;
  default:
    return false;
  };
}

std::vector<std::pair<int, identifier_t>> extract_ids(
    const std::vector<expr_t *> &dims) {
  std::vector<std::pair<int, identifier_t>> refs;
  int i = 0;
  for (auto dim : dims) {
    if (var_t *var = dcast<var_t *>(dim)) {
      if (var->id.name != "_") {
        refs.push_back({i, var->id});
      }
    } else {
      throw user_error(dim->get_location(),
                       "only reference expressions are allowed here");
    }
    ++i;
  }
  assert(refs.size() != 0);
  return refs;
}

void unfold_application_exprs(expr_t *e, std::vector<expr_t *> &exprs) {
  auto app = dcast<application_t *>(e);
  if (app != nullptr) {
    unfold_application_exprs(app->a, exprs);
    exprs.push_back(app->b);
  } else {
    exprs.push_back(e);
  }
}

predicate_t *convert_tuple_into_predicate(tuple_t *tuple) {
  std::vector<predicate_t *> params;
  for (auto dim : tuple->dims) {
    params.push_back(convert_expr_to_predicate(dim));
  }
  return new tuple_predicate_t(tuple->get_location(), params,
                               maybe<identifier_t>{});
}

predicate_t *convert_var_to_predicate(var_t *var) {
  return new irrefutable_predicate_t(var->id.location,
                                     maybe<identifier_t>(var->id));
}

predicate_t *convert_expr_to_predicate(expr_t *expr) {
  if (auto application = dcast<application_t *>(expr)) {
    return unfold_application_into_predicate(application);
  } else if (auto tuple = dcast<tuple_t *>(expr)) {
    return convert_tuple_into_predicate(tuple);
  } else if (auto var = dcast<var_t *>(expr)) {
    return convert_var_to_predicate(var);
  } else {
    throw user_error(expr->get_location(),
                     "zion parser is unsure how to rewrite this destructuring");
  }
}

predicate_t *unfold_application_into_predicate(application_t *application) {
  std::vector<expr_t *> exprs;
  unfold_application_exprs(application, exprs);
  if (exprs.size() >= 1) {
    if (var_t *var = dcast<var_t *>(exprs[0])) {
      /* this may be a data constructor, treat it as such */
      auto ctor_name = var->id;
      std::vector<predicate_t *> params;
      for (int i = 1; i < exprs.size(); ++i) {
        params.push_back(convert_expr_to_predicate(exprs[i]));
      }
      return new ctor_predicate_t(ctor_name.location, params, ctor_name,
                                  maybe<identifier_t>{});
    } else {
      log_location(exprs[0]->get_location(), "found %s not sure what do",
                   exprs[0]->str().c_str());
      assert(false);
    }
  } else {
    throw user_error(
        exprs[0]->get_location(),
        "invalid syntax. you can't destructure an application here");
  }
  assert(false);
  return nullptr;
}

expr_t *parse_assign_ctor_destructure(parse_state_t &ps,
                                      application_t *application) {
  chomp_token(tk_becomes);

  // See if the application can be reversed into a ctor_predicate
  predicate_t *predicate = unfold_application_into_predicate(application);
  expr_t *rhs = parse_expr(ps);
  expr_t *body = parse_block(ps, false /*expression_means_return*/);
  return new match_t(rhs, {new pattern_block_t(predicate, body)});
}

expr_t *parse_assign_tuple_destructure(parse_state_t &ps, tuple_t *tuple) {
  eat_token();

  predicate_t *predicate = convert_tuple_into_predicate(tuple);
  expr_t *rhs = parse_expr(ps);
  expr_t *body = parse_block(ps, false /*expression_means_return*/);
  return new match_t(rhs, {new pattern_block_t(predicate, body)});
}

expr_t *parse_var_decl(parse_state_t &ps,
                       bool is_let,
                       bool allow_tuple_destructuring) {
  if (ps.token.tk == tk_lparen) {
    if (!is_let) {
      throw user_error(
          ps.token.location,
          "mutable destructuring design needs work... please log an issue");
    }
    if (!allow_tuple_destructuring) {
      throw user_error(ps.token.location, "destructuring is not allowed here");
    }

    auto prior_token = ps.token;
    auto tuple = dcast<tuple_t *>(parse_tuple_expr(ps));
    if (tuple == nullptr) {
      throw user_error(
          prior_token.location,
          "tuple destructuring detected an invalid expression on the lhs");
    }

    if (ps.token.tk == tk_assign) {
      return parse_assign_tuple_destructure(ps, tuple);
    } else {
      throw user_error(ps.token.location,
                       "destructured tuples must be assigned to an rhs");
    }
  } else {
    expect_token(tk_identifier);

    identifier_t var_id = identifier_t::from_token(ps.token);
    ps.advance();
    return parse_let(ps, var_id, is_let);
  }
}

expr_t *parse_let(parse_state_t &ps, identifier_t var_id, bool is_let) {
  auto location = ps.token.location;
  expr_t *initializer = nullptr;

  if (!ps.line_broke() &&
      (ps.token.tk == tk_assign || ps.token.tk == tk_becomes)) {
    eat_token();
    initializer = parse_expr(ps);
  } else {
    initializer = new application_t(
        new var_t(ps.id_mapped(identifier_t{"new", location})),
        unit_expr(INTERNAL_LOC()));
  }

  if (ps.token.is_ident(K(as))) {
    /* allow type specifications in decls to help with inference */
    ps.advance();
    initializer = new as_t(initializer, scheme({}, {}, parse_type(ps)),
                           false /*force_cast*/);
  }

  if (!is_let) {
    initializer = new application_t(
        new var_t(ps.id_mapped(identifier_t{"Ref", location})), initializer);
  }

  return new let_t(var_id, initializer,
                   parse_block(ps, false /*expression_means_return*/));
}

expr_t *parse_return_statement(parse_state_t &ps) {
  auto return_token = ps.token;
  chomp_ident(K(return ));
  return new return_statement_t((!ps.line_broke() && ps.token.tk != tk_rcurly)
                                    ? parse_expr(ps)
                                    : unit_expr(INTERNAL_LOC()));
}

maybe<identifier_t> parse_with_param(parse_state_t &ps, expr_t *&expr) {
  expr = parse_expr(ps);
  if (auto var = dcast<var_t *>(expr)) {
    if (ps.token.tk == tk_becomes) {
      auto param_id = var->id;
      ps.advance();
      expr = parse_expr(ps);
      return maybe<identifier_t>(param_id);
    }
  }
  return maybe<identifier_t>();
}

expr_t *parse_with_block(parse_state_t &ps) {
  return unit_expr(INTERNAL_LOC());
#if 0
	auto with_token = ps.token;
	ps.advance();

	expr_t *expr = nullptr;
	maybe<identifier_t> maybe_param_id = parse_with_param(ps, expr);
	assert(expr != nullptr);

	identifier_t param_id = (maybe_param_id.valid
			? maybe_param_id.t
			: identifier_t{fresh(), with_token.location});

	auto block = parse_block(ps, false /*expression_means_return*/);

	auto else_token = ps.token;
	chomp_ident(K(else));

	identifier_t error_var_id = (ps.token.tk == tk_identifier)
		? identifier_t::from_token(ps.token_and_advance())
		: identifier_t{fresh(), with_token.location};

	auto error_block = parse_block(ps, false /* expression_means_return */);

	auto cleanup_token = identifier_t{"__cleanup", with_token.location};
	auto match = create<match_expr_t>(with_token);
	match->value = expr;

	auto with_pattern = create<pattern_block_t>(with_token);
	with_pattern->block = block;

	auto with_predicate = create<ctor_predicate_t>(Token{with_token.location, tk_identifier, "Acquired"});
	with_predicate->params.push_back(create<irrefutable_predicate_t>(param_id));
	with_predicate->params.push_back(create<irrefutable_predicate_t>(cleanup_token));
	with_pattern->predicate = with_predicate;

	auto cleanup_defer = create<defer_t>(block->token);
	cleanup_defer->callable = create<reference_expr_t>(cleanup_token);
	block->statements.insert(block->statements.begin(), cleanup_defer);

	auto else_pattern = create<pattern_block_t>(else_token);
	else_pattern->block = error_block;

	auto else_predicate = create<ctor_predicate_t>(Token{else_token.location, tk_identifier, "Failed"});
	else_predicate->params.push_back(create<irrefutable_predicate_t>(error_var_id));
	else_pattern->predicate = else_predicate;

	match->pattern_blocks.push_back(with_pattern);
	match->pattern_blocks.push_back(else_pattern);
	return match;
#endif
}

expr_t *wrap_with_iter(parse_state_t &ps, expr_t *expr) {
  return new application_t(
      new var_t(ps.id_mapped(identifier_t{"iter", expr->get_location()})),
      expr);
}

expr_t *parse_for_block(parse_state_t &ps) {
  chomp_ident("for");

  if (ps.token.tk == tk_lparen) {
    // TODO: handle destructuring tuples
    assert(false);
    return nullptr;
  } else {
    expect_token(tk_identifier);
    auto var = iid(ps.token_and_advance());
    auto in_token = ps.token;
    chomp_ident(K(in));
    auto iterable = parse_expr(ps);
    auto block = parse_block(ps, false /*expression_means_return*/);
    auto iterator_id = identifier_t{fresh(), var.location};
    return new let_t(
        iterator_id,
        new application_t(
            new var_t(ps.id_mapped(identifier_t{"iter", in_token.location})),
            iterable),
        new while_t(
            new var_t(ps.id_mapped(identifier_t{"True", in_token.location})),
            new match_t(
                new application_t(new var_t(iterator_id),
                                  unit_expr(iterator_id.location)),
                {new pattern_block_t(
                     new ctor_predicate_t(
                         iterator_id.location,
                         {new irrefutable_predicate_t(
                             var.location, maybe<identifier_t>(var))},
                         ps.id_mapped(
                             identifier_t{"Just", iterator_id.location}),
                         maybe<identifier_t>()),
                     block),
                 new pattern_block_t(
                     new ctor_predicate_t(iterator_id.location, {},
                                          ps.id_mapped(identifier_t{
                                              "Nothing", iterator_id.location}),
                                          maybe<identifier_t>()),
                     new break_t(in_token.location))})));
  }
}

expr_t *parse_defer(parse_state_t &ps) {
  return unit_expr(INTERNAL_LOC());
#if 0
	auto defer = create<defer_t>(ps.token);
	ps.advance();
	defer->callable = expression_t::parse(ps);
	return defer;
#endif
}

expr_t *parse_new_expr(parse_state_t &ps) {
  ps.advance();
  return new as_t(new application_t(
                      new var_t(ps.id_mapped({"new", ps.prior_token.location})),
                      unit_expr(ps.token.location)),
                  scheme({}, {}, parse_type(ps)), false /*force_cast*/);
}

expr_t *parse_static_print(parse_state_t &ps) {
  auto location = ps.token_and_advance().location;
  chomp_token(tk_lparen);
  auto sp = new static_print_t(location, parse_expr(ps));
  chomp_token(tk_rparen);
  return sp;
}

// assert macro expansion. should avoid lib/std for
expr_t *parse_assert(parse_state_t &ps) {
  Token assert_token = ps.token;
  chomp_ident(K(assert));
  chomp_token(tk_lparen);

  expr_t *condition = parse_expr(ps);
  std::string assert_message = string_format(
      "%s: assertion failed: (%s)\n", ps.token.location.repr().c_str(),
      clean_ansi_escapes(condition->str()).c_str());
  expr_t *assertion = new conditional_t(
      condition, // The condition we are asserting
      unit_expr(ps.token.location),
      new block_t({
          new as_t(
              new as_t(new builtin_t(
                           new var_t(identifier_t{"__builtin_ffi_3",
                                                  ps.token.location}),
                           {
                               new literal_t(Token{ps.token.location, tk_string,
                                                   escape_json_quotes("writ"
                                                                      "e")}),
                               new literal_t(Token{ps.token.location,
                                                   tk_integer, "2" /*stderr*/}),
                               new literal_t(
                                   Token{ps.token.location, tk_string,
                                         escape_json_quotes(assert_message)}),
                               new literal_t(Token{
                                   ps.token.location, tk_integer,
                                   std::to_string(assert_message.size())}),
                           }),
                       type_id(make_iid(INT_TYPE))->generalize({}),
                       false /*force_cast*/),
              type_unit(INTERNAL_LOC())->generalize({}), true /*force_cast*/),
          new builtin_t(
              new var_t(make_iid("__builtin_ffi_1")),
              {new literal_t(
                   Token{assert_token.location, tk_string, "\"exit\""}),
               new literal_t(Token{assert_token.location, tk_integer, "1"})}),
          unit_expr(ps.token.location),
      }));
  chomp_token(tk_rparen);
  return assertion;
}

expr_t *parse_statement(parse_state_t &ps) {
  assert(ps.token.tk != tk_rcurly);

  if (ps.token.is_ident(K(var))) {
    ps.advance();
    return parse_var_decl(ps, false /*is_let*/,
                          true /*allow_tuple_destructuring*/);
  } else if (ps.token.is_ident(K(let))) {
    ps.advance();
    return parse_var_decl(ps, true /*is_let*/,
                          true /*allow_tuple_destructuring*/);
  } else if (ps.token.is_ident(K(if))) {
    return parse_if(ps);
  } else if (ps.token.is_ident(K(assert))) {
    return parse_assert(ps);
  } else if (ps.token.is_ident("while")) {
    return parse_while(ps);
  } else if (ps.token.is_ident("for")) {
    return parse_for_block(ps);
  } else if (ps.token.is_ident(K(match))) {
    return parse_match(ps);
  } else if (ps.token.is_ident(K(new))) {
    return parse_new_expr(ps);
  } else if (ps.token.is_ident(K(static_print))) {
    return parse_static_print(ps);
  } else if (ps.token.is_ident(K(fn))) {
    ps.advance();
    if (ps.token.tk == tk_identifier) {
      return new let_t(identifier_t::from_token(ps.token), parse_lambda(ps),
                       parse_block(ps, false /*expression_means_return*/));
    } else {
      return parse_lambda(ps);
    }
  } else if (ps.token.is_ident(K(return ))) {
    return parse_return_statement(ps);
  } else if (ps.token.is_ident(K(unreachable))) {
    return new var_t(iid(ps.token));
  } else if (ps.token.is_ident(K(continue))) {
    return new continue_t(ps.token_and_advance().location);
  } else if (ps.token.is_ident(K(break))) {
    return new break_t(ps.token_and_advance().location);
  } else {
    return parse_assignment(ps);
  }
}

expr_t *parse_var_ref(parse_state_t &ps) {
  // TODO: if this name is a var, then treat it as a load
  if (ps.token.tk != tk_identifier) {
    throw user_error(ps.token.location, "expected an identifier");
  }

  if (ps.token.is_ident(K(__filename__))) {
    auto token = ps.token_and_advance();
    return new literal_t(Token{token.location, tk_string,
                               escape_json_quotes(token.location.filename)});
  } else if (in(ps.token.text, ps.builtin_arities)) {
    RawParseMode rpm(ps);
    int arity = get(ps.builtin_arities, ps.token.text, -1);
    assert(arity >= 0);
    auto builtin_token = ps.token_and_advance();
    std::vector<expr_t *> exprs;
    if (arity > 0) {
      chomp_token(tk_lparen);
      while (true) {
        exprs.push_back(parse_expr(ps));
        --arity;
        if (arity > 0) {
          chomp_token(tk_comma);
          continue;
        } else {
          chomp_token(tk_rparen);
          break;
        }
      }
    }

    return new builtin_t(new var_t(iid(builtin_token)), exprs);
  } else if (ps.token.text == "__host_int") {
    RawParseMode rpm(ps);
    ps.advance();
    chomp_token(tk_lparen);
    expect_token(tk_identifier);
    location_t location = ps.token.location;
    int value = get_host_int(location, ps.token_and_advance().text);
    chomp_token(tk_rparen);
    return new literal_t(Token{location, tk_integer, std::to_string(value)});
  }

  if (ps.token.is_ident(K(if))) {
    throw user_error(ps.token.location,
                     "if statements cannot be used as expressions. use the "
                     "ternary operator ?:");
  } else if (ps.token.is_ident(K(while))) {
    throw user_error(ps.token.location,
                     "%s statements cannot be used as expressions",
                     ps.token.text.c_str());
  }
  return new var_t(ps.identifier_and_advance());
}

expr_t *parse_base_expr(parse_state_t &ps) {
  if (ps.token.tk == tk_lparen) {
    return parse_tuple_expr(ps);
  } else if (ps.token.is_ident(K(new))) {
    return parse_new_expr(ps);
  } else if (ps.token.is_ident(K(fn))) {
    ps.advance();
    return parse_lambda(ps);
  } else if (ps.token.is_ident(K(fix))) {
    ps.advance();
    return new fix_t(parse_base_expr(ps));
  } else if (ps.token.is_ident(K(match))) {
    return parse_match(ps);
  } else if (ps.token.is_ident(K(null))) {
    return new as_t(
        new literal_t(Token{ps.token_and_advance().location, tk_integer, "0"}),
        scheme({"a"}, {},
               type_ptr(
                   type_variable(identifier_t{"a", ps.prior_token.location}))),
        true /*force_cast*/);
  } else if (ps.token.tk == tk_identifier) {
    return parse_var_ref(ps);
  } else {
    return parse_literal(ps);
  }
}

expr_t *parse_array_literal(parse_state_t &ps) {
  location_t location = ps.token.location;
  chomp_token(tk_lsquare);
  std::vector<expr_t *> exprs;

  int i = 0;
  while (ps.token.tk != tk_rsquare && ps.token.tk != tk_none) {
    ++i;
    exprs.push_back(parse_expr(ps));

    if (ps.token.tk == tk_double_dot && (i == 1 || i == 2)) {
      /* range syntax with step calculation */
      auto location = ps.token.location;
      ps.advance();
      /* let range_min = exprs[0] in let range_next = {exprs[1] or
       * (range_min+1)} in let range_max = {exprs[1] or Max Int} in
       * Range(range_min, range_next-range_min, range_max) */

      identifier_t range_min = identifier_t("__range_min" + fresh(),
                                            ps.token.location);
      identifier_t range_next = identifier_t("__range_next" + fresh(),
                                             ps.token.location);
      identifier_t range_max = identifier_t("__range_max" + fresh(),
                                            ps.token.location);

      auto range_body = new application_t(
          new application_t(
              new application_t(
                  new var_t(ps.id_mapped(identifier_t{"Range", location})),
                  new var_t(range_min)),
              new application_t(
                  new application_t(
                      new var_t(identifier_t("std.-", ps.token.location)),
                      new var_t(range_next)),
                  new var_t(range_min))),
          new var_t(range_max));

      auto let_range_max = new let_t(
          range_max,
          (ps.token.tk != tk_rsquare)
              ? parse_expr(ps)
              : new application_t(
                    new var_t(ps.id_mapped(make_iid("max_bound"))),
                    unit_expr(location)),
          range_body);

      auto let_range_next = new let_t(
          range_next,
          (i == 2) ? exprs[1]
                   : new application_t(
                         new application_t(
                             new var_t(make_iid("std.+")),
                             new literal_t(Token{location, tk_integer, "1"})),
                         new var_t(range_min)),
          let_range_max);

      auto let_range_min = new let_t(range_min, exprs[0], let_range_next);

      chomp_token(tk_rsquare);
      return let_range_min;
    } else if (ps.token.tk == tk_comma) {
      ps.advance();
    } else if (ps.token.tk != tk_rsquare) {
      throw user_error(
          ps.token.location,
          "found something (%s) that does not make sense in an array literal",
          ps.token.str().c_str());
    }
  }
  chomp_token(tk_rsquare);

  auto array_var = new var_t(identifier_t(fresh(), ps.token.location));

  /* take all the exprs from the array, and turn them into statements to fill
   * out a vector */
  std::vector<expr_t *> stmts;
  for (auto expr : exprs) {
    stmts.push_back(new application_t(
        new application_t(
            new var_t(ps.id_mapped(identifier_t{"append", ps.token.location})),
            array_var),
        expr));
  }

  const auto array_size_to_reserve = string_format("%d", exprs.size());

  /* now, add another item just for the actual array value to be returned */
  stmts.push_back(array_var);

  return new let_t(
      array_var->id,
      new as_t(new application_t(
                   new var_t(ps.id_mapped({"new", ps.prior_token.location})),
                   unit_expr(ps.token.location)),
               scheme({"a"}, {},
                      type_vector_type(
                          type_variable(identifier_t("a", ps.token.location)))),
               false /*force_cast*/),
      new block_t(stmts));
}

expr_t *parse_literal(parse_state_t &ps) {
  switch (ps.token.tk) {
  case tk_integer:
    return new literal_t(ps.token_and_advance());
  case tk_char:
  case tk_float:
    return new literal_t(ps.token_and_advance());
  case tk_string: {
    auto token = ps.token_and_advance();
    if (ps.sugar_literals) {
      int string_len = unescape_json_quotes(token.text).size();
      return new application_t(
          new application_t(
              new var_t(identifier_t{"std.String", token.location}),
              new literal_t(token)),
          new literal_t(
              Token{token.location, tk_integer, std::to_string(string_len)}));
    } else {
      return new literal_t(token);
    }
  }
  case tk_lsquare:
    if (ps.sugar_literals) {
      return parse_array_literal(ps);
    } else {
      throw user_error(ps.token.location,
                       "array literals are not implemented in "
                       "this parse context");
    }
    // case tk_lcurly:
    //	return assoc_array_expr_t::parse(ps);

  case tk_identifier:
    throw user_error(ps.token.location,
                     "unexpected token found when parsing literal expression. "
                     "'" c_error("%s") "'",
                     ps.token.text.c_str());

  default:
    if (ps.token.tk == tk_lcurly) {
      throw user_error(ps.token.location, "this squiggly brace is a surprise");
    } else if (ps.lexer.eof()) {
      auto error = user_error(ps.token.location, "unexpected end-of-file.");
      for (auto pair : ps.lexer.nested_tks) {
        error.add_info(pair.first, "unclosed %s here", tkstr(pair.second));
      }
      throw error;
    } else {
      throw user_error(ps.token.location,
                       "out of place token found when parsing literal "
                       "expression. '" c_error("%s") "' (%s)",
                       ps.token.text.c_str(), tkstr(ps.token.tk));
    }
  }
}

expr_t *parse_postfix_expr(parse_state_t &ps) {
  expr_t *expr = parse_base_expr(ps);

  while (!ps.line_broke() &&
         (ps.token.tk == tk_lsquare || ps.token.tk == tk_lparen ||
          ps.token.tk == tk_dot)) {
    switch (ps.token.tk) {
    case tk_lparen: {
      /* function call */
      auto location = ps.token.location;
      ps.advance();
      if (ps.token.tk == tk_rparen) {
        ps.advance();
        expr = new application_t(expr, unit_expr(ps.token.location));
      } else {
        while (ps.token.tk != tk_rparen) {
          expr = new application_t(expr, parse_expr(ps));
          if (ps.token.tk == tk_comma) {
            ps.advance();
          } else {
            expect_token(tk_rparen);
          }
        }
        ps.advance();
      }
      break;
    }
    case tk_dot: {
      ps.advance();
      expect_token(tk_identifier);
      if (!islower(ps.token.text[0])) {
        throw user_error(
            ps.token.location,
            "property accessors must start with lowercase letters");
      }
      auto iid = identifier_t{"__get_" + ps.token_and_advance().text,
                              ps.prior_token.location};
      expr = new application_t(new var_t(iid), expr);
      break;
    }
    case tk_lsquare: {
      ps.advance();
      bool is_slice = false;

      expr_t *start = parse_expr(ps);

      if (ps.token.tk == tk_colon) {
        is_slice = true;
        ps.advance();
      }

      if (ps.token.tk == tk_rsquare) {
        ps.advance();
        if (ps.token.tk == tk_assign) {
          /* set up an array index assignment */
          auto location = ps.token_and_advance().location;
          auto rhs = parse_expr(ps);
          expr = new application_t(
              new application_t(
                  new application_t(
                      new var_t({"std.set_indexed_item", location}), expr),
                  start),
              rhs);
        } else {
          expr = new application_t(
              new application_t(
                  new var_t(identifier_t{is_slice ? "__getslice2__"
                                                  : "std.get_indexed_item",
                                         ps.token.location}),
                  expr),
              start);
        }
      } else {
        expr_t *stop = parse_expr(ps);
        chomp_token(tk_rsquare);

        assert(is_slice);
        expr = new application_t(
            new application_t(
                new application_t(new var_t(ps.id_mapped(identifier_t{
                                      "__getslice3__", ps.token.location})),
                                  expr),
                start),
            stop);
      }
      break;
    }
    default:
      break;
    }
  }

  return expr;
}

expr_t *parse_cast_expr(parse_state_t &ps) {
  expr_t *expr = parse_postfix_expr(ps);
  while (!ps.line_broke() && ps.token.is_ident(K(as))) {
    ps.advance();
    bool force_cast = false;
    if (ps.token.tk == tk_bang && ps.token.follows_after(ps.prior_token)) {
      /* this is a force-cast */
      force_cast = true;
      ps.advance();
    }
    expr = new as_t(expr, scheme({}, {}, parse_type(ps)), force_cast);
  }
  return expr;
}

expr_t *parse_sizeof(parse_state_t &ps) {
  auto location = ps.token.location;
  ps.advance();
  chomp_token(tk_lparen);
  auto type = parse_type(ps);
  chomp_token(tk_rparen);
  return new sizeof_t(location, type);
}

expr_t *parse_prefix_expr(parse_state_t &ps) {
  if (ps.token.is_ident(K(sizeof))) {
    return parse_sizeof(ps);
  }

  maybe<Token> prefix = (ps.token.tk == tk_minus || ps.token.is_ident(K(not)) ||
                         ps.token.tk == tk_bang)
                            ? maybe<Token>(ps.token)
                            : maybe<Token>();

  if (prefix.valid) {
    ps.advance();
  }

  expr_t *rhs;
  if (ps.token.is_ident(K(not)) || ps.token.tk == tk_minus ||
      ps.token.tk == tk_bang) {
    /* recurse to find more prefix expressions */
    rhs = parse_prefix_expr(ps);
  } else {
    /* ok, we're done with prefix operators */
    rhs = parse_cast_expr(ps);
  }

  if (prefix.valid) {
    if (prefix.t.text == "-") {
      return new application_t(
          new var_t(ps.id_mapped(identifier_t{"negate", prefix.t.location})),
          rhs);
    } else if (prefix.t.text == "!") {
      return new application_t(
          new var_t(identifier_t{"std.load_value", prefix.t.location}), rhs);
    } else {
      return new application_t(new var_t(ps.id_mapped(identifier_t{
                                   prefix.t.text, prefix.t.location})),
                               rhs);
    }
  } else {
    return rhs;
  }
}

expr_t *parse_times_expr(parse_state_t &ps) {
  expr_t *expr = parse_prefix_expr(ps);

  while (!ps.line_broke() &&
         (ps.token.tk == tk_times || ps.token.tk == tk_divide_by ||
          ps.token.tk == tk_mod)) {
    identifier_t op = ps.id_mapped({ps.token.text, ps.token.location});
    ps.advance();

    expr = new application_t(new application_t(new var_t(op), expr),
                             parse_prefix_expr(ps));
  }

  return expr;
}

expr_t *parse_plus_expr(parse_state_t &ps) {
  auto expr = parse_times_expr(ps);

  while (!ps.line_broke() &&
         (ps.token.tk == tk_plus || ps.token.tk == tk_minus ||
          ps.token.tk == tk_backslash)) {
    identifier_t op = ps.id_mapped({ps.token.text, ps.token.location});
    ps.advance();

    expr = new application_t(new application_t(new var_t(op), expr),
                             parse_times_expr(ps));
  }

  return expr;
}

expr_t *parse_shift_expr(parse_state_t &ps) {
  auto expr = parse_plus_expr(ps);

  while (!ps.line_broke() &&
         (ps.token.tk == tk_shift_left || ps.token.tk == tk_shift_right)) {
    identifier_t op = ps.id_mapped({ps.token.text, ps.token.location});
    ps.advance();

    expr = new application_t(new application_t(new var_t(op), expr),
                             parse_plus_expr(ps));
  }

  return expr;
}

expr_t *parse_binary_eq_expr(parse_state_t &ps) {
  auto lhs = parse_shift_expr(ps);
  if (ps.line_broke() ||
      !(ps.token.tk == tk_binary_equal || ps.token.tk == tk_binary_inequal)) {
    /* there is no rhs */
    return lhs;
  }

  identifier_t op = ps.id_mapped({ps.token.text, ps.token.location});
  ps.advance();

  return new application_t(new application_t(new var_t(op), lhs),
                           parse_shift_expr(ps));
}

expr_t *parse_ineq_expr(parse_state_t &ps) {
  auto lhs = parse_binary_eq_expr(ps);
  if (ps.line_broke() || !(ps.token.tk == tk_gt || ps.token.tk == tk_gte ||
                           ps.token.tk == tk_lt || ps.token.tk == tk_lte)) {
    /* there is no rhs */
    return lhs;
  }

  identifier_t op = ps.id_mapped({ps.token.text, ps.token.location});
  ps.advance();

  return new application_t(new application_t(new var_t(op), lhs),
                           parse_shift_expr(ps));
}

expr_t *parse_eq_expr(parse_state_t &ps) {
  auto lhs = parse_ineq_expr(ps);
  bool not_in = false;
  if (ps.token.is_ident(K(not))) {
    eat_token();
    expect_ident(K(in));
    not_in = true;
  }

  if (ps.line_broke() ||
      !(ps.token.is_ident(K(in)) || ps.token.tk == tk_equal ||
        ps.token.tk == tk_inequal)) {
    /* there is no rhs */
    return lhs;
  }

  identifier_t op = ps.id_mapped(
      {not_in ? "not-in" : ps.token.text, ps.token.location});
  ps.advance();

  return new application_t(new application_t(new var_t(op), lhs),
                           parse_ineq_expr(ps));
}

expr_t *parse_bitwise_and(parse_state_t &ps) {
  auto expr = parse_eq_expr(ps);

  while (!ps.line_broke() && ps.token.tk == tk_ampersand) {
    identifier_t op = ps.id_mapped({ps.token.text, ps.token.location});
    ps.advance();

    expr = new application_t(new application_t(new var_t(op), expr),
                             parse_eq_expr(ps));
  }

  return expr;
}

expr_t *parse_bitwise_xor(parse_state_t &ps) {
  auto expr = parse_bitwise_and(ps);

  while (!ps.line_broke() && ps.token.tk == tk_hat) {
    identifier_t op = ps.id_mapped({ps.token.text, ps.token.location});
    ps.advance();

    expr = new application_t(new application_t(new var_t(op), expr),
                             parse_bitwise_and(ps));
  }
  return expr;
}

expr_t *parse_bitwise_or(parse_state_t &ps) {
  auto expr = parse_bitwise_xor(ps);

  while (!ps.line_broke() && ps.token.tk == tk_pipe) {
    identifier_t op = ps.id_mapped({ps.token.text, ps.token.location});
    ps.advance();

    expr = new application_t(new application_t(new var_t(op), expr),
                             parse_bitwise_xor(ps));
  }

  return expr;
}

expr_t *fold_and_exprs(std::vector<expr_t *> exprs, int index) {
  if (index < exprs.size() - 1) {
    identifier_t term_id = make_iid(fresh());
    return new let_t(term_id, exprs[index],
                     new conditional_t(new var_t(term_id),
                                       fold_and_exprs(exprs, index + 1),
                                       new var_t(make_iid("std.False"))));
  } else {
    return exprs[index];
  }
}

expr_t *fold_or_exprs(std::vector<expr_t *> exprs, int index) {
  if (index < exprs.size() - 1) {
    identifier_t term_id = make_iid(fresh());
    return new let_t(term_id, exprs[index],
                     new conditional_t(new var_t(term_id),
                                       new var_t(make_iid("std.True")),
                                       fold_or_exprs(exprs, index + 1)));
  } else {
    return exprs[index];
  }
}

expr_t *parse_and_expr(parse_state_t &ps) {
  std::vector<expr_t *> exprs;
  exprs.push_back(parse_bitwise_or(ps));

  while (!ps.line_broke() && (ps.token.is_ident(K(and)))) {
    ps.advance();
    exprs.push_back(parse_bitwise_or(ps));
  }

  return fold_and_exprs(exprs, 0);
}

expr_t *parse_tuple_expr(parse_state_t &ps) {
  auto start_token = ps.token;
  chomp_token(tk_lparen);
  if (ps.token.tk == tk_rparen) {
    /* we've got a reference to sole value of unit type */
    return unit_expr(ps.token_and_advance().location);
  }
  expr_t *expr = parse_expr(ps);
  if (ps.token.tk != tk_comma) {
    chomp_token(tk_rparen);
    return expr;
  } else {
    ps.advance();

    std::vector<expr_t *> exprs;

    exprs.push_back(expr);

    /* now let's find the rest of the values */
    while (true) {
      if (ps.token.tk == tk_rparen) {
        ps.advance();
        break;
      }
      exprs.push_back(parse_expr(ps));
      if (ps.token.tk == tk_comma) {
        ps.advance();
      }
      // continue and read the next parameter
    }

    return new tuple_t(start_token.location, exprs);
  }
}

expr_t *parse_or_expr(parse_state_t &ps) {
  std::vector<expr_t *> exprs;
  exprs.push_back(parse_and_expr(ps));

  while (!ps.line_broke() && (ps.token.is_ident(K(or)))) {
    ps.advance();
    exprs.push_back(parse_and_expr(ps));
  }

  return fold_or_exprs(exprs, 0);
}

expr_t *parse_ternary_expr(parse_state_t &ps) {
  expr_t *condition = parse_or_expr(ps);
  if (ps.token.tk == tk_maybe) {
    ps.advance();

    expr_t *truthy_expr = parse_or_expr(ps);
    expect_token(tk_colon);
    ps.advance();
    return new conditional_t(condition, truthy_expr, parse_expr(ps));
  } else {
    return condition;
  }
}

expr_t *parse_expr(parse_state_t &ps) {
  return parse_ternary_expr(ps);
}

expr_t *parse_assignment(parse_state_t &ps) {
  expr_t *lhs = parse_expr(ps);

  if (ps.line_broke()) {
    return lhs;
  }

  switch (ps.token.tk) {
  case tk_assign:
    ps.advance();
    return new application_t(
        new application_t(
            new var_t(identifier_t{"std.store_value", ps.token.location}), lhs),
        parse_expr(ps));
  case tk_divide_by_eq:
  case tk_minus_eq:
  case tk_mod_eq:
  case tk_plus_eq:
  case tk_times_eq: {
    auto op_token = ps.token_and_advance();
    assert(op_token.text.size() >= 1);
    expr_t *rhs = parse_expr(ps);
    identifier_t copy_value = identifier_t{fresh(), lhs->get_location()};
    return new application_t(
        new application_t(
            new var_t(identifier_t{"std.store_value", op_token.location}), lhs),
        new let_t(
            copy_value,
            new application_t(
                new var_t(identifier_t{"std.load_value", op_token.location}),
                lhs),
            new application_t(new application_t(new var_t(ps.id_mapped(
                                                    {op_token.text.substr(0, 1),
                                                     op_token.location})),
                                                new var_t(copy_value)),
                              rhs)));
  }
  case tk_becomes:
    if (var_t *var = dcast<var_t *>(lhs)) {
      return parse_let(ps, var->id, true /* is_let */);
    } else if (auto tuple = dcast<tuple_t *>(lhs)) {
      return parse_assign_tuple_destructure(ps, tuple);
    } else if (auto application = dcast<application_t *>(lhs)) {
      return parse_assign_ctor_destructure(ps, application);
    } else {
      throw user_error(ps.token.location,
                       ":= may only come after a new symbol name");
    }
  default:
    return lhs;
  }
}

expr_t *parse_block(parse_state_t &ps, bool expression_means_return) {
  bool expression_block_syntax = false;
  Token expression_block_assign_token;
  bool finish_block = false;
  if (ps.token.tk == tk_lcurly) {
    finish_block = true;
    ps.advance();
    if (ps.token.tk == tk_rcurly) {
      if (expression_means_return) {
        return new return_statement_t(
            unit_expr(ps.token_and_advance().location));
      } else {
        return unit_expr(ps.token_and_advance().location);
      }
    }
  } else if (ps.token.tk == tk_expr_block) {
    expression_block_syntax = true;
    expression_block_assign_token = ps.token;
    ps.advance();
  }

  if (expression_block_syntax) {
    if (!ps.line_broke()) {
      auto statement = parse_statement(ps);
      if (expression_means_return) {
        if (auto expression = dcast<expr_t *>(statement)) {
          auto return_statement = new return_statement_t(expression);
          statement = return_statement;
        }
      }

      if (ps.token.tk != tk_rparen && ps.token.tk != tk_rcurly &&
          ps.token.tk != tk_rsquare && ps.token.tk != tk_comma &&
          !ps.line_broke()) {
        throw user_error(ps.token.location,
                         "this looks hard to read. you should have a line "
                         "break after = blocks, unless they are immediately "
                         "followed by one of these: )]}");
      }
      return statement;
    } else {
      throw user_error(ps.token.location,
                       "empty expression blocks are not allowed");
    }
  } else {
    std::vector<expr_t *> stmts;
    while (ps.token.tk != tk_rcurly) {
      while (ps.token.tk == tk_semicolon) {
        ps.advance();
      }
      stmts.push_back(parse_statement(ps));
      while (ps.token.tk == tk_semicolon) {
        ps.advance();
      }
    }
    if (finish_block) {
      chomp_token(tk_rcurly);
    }
    if (stmts.size() == 0) {
      return unit_expr(ps.token.location);
    } else if (stmts.size() == 1) {
      return stmts[0];
    } else {
      return new block_t(stmts);
    }
  }
}

conditional_t *parse_if(parse_state_t &ps) {
  if (ps.token.is_ident(K(if))) {
    ps.advance();
  } else {
    throw user_error(ps.token.location, "expected if");
  }

  Token condition_token = ps.token;
  expr_t *condition = parse_expr(ps);
  expr_t *block = parse_block(ps, false /*expression_means_return*/);
  expr_t *else_ = nullptr;
  /* check the successive instructions for "else if" or else */
  if (ps.token.is_ident(K(else))) {
    ps.advance();
    if (ps.token.is_ident(K(if))) {
      if (ps.line_broke()) {
        throw user_error(ps.token.location, "else if must be on the same line");
      }
      else_ = parse_if(ps);
    } else {
      else_ = parse_block(ps, false /*expression_means_return*/);
    }
  }

  return new conditional_t(condition, block,
                           else_ != nullptr ? else_
                                            : unit_expr(ps.token.location));
}

while_t *parse_while(parse_state_t &ps) {
  auto while_token = ps.token;
  chomp_ident(K(while));
  Token condition_token = ps.token;
  if (condition_token.is_ident(K(match))) {
    /* sugar for while match ... which becomes while true { match ... } */
    return new while_t(
        new var_t(ps.id_mapped(identifier_t{"True", while_token.location})),
        parse_match(ps));
  } else {
    return new while_t(parse_expr(ps),
                       parse_block(ps, false /*expression_means_return*/));
  }
}

predicate_t *parse_ctor_predicate(parse_state_t &ps,
                                  maybe<identifier_t> name_assignment) {
  assert(ps.token.tk == tk_identifier && isupper(ps.token.text[0]));
  identifier_t ctor_name = ps.identifier_and_advance();

  std::vector<predicate_t *> params;
  if (ps.token.tk == tk_lparen) {
    ps.advance();
    while (ps.token.tk != tk_rparen) {
      params.push_back(parse_predicate(
          ps, false /*allow_else*/, maybe<identifier_t>() /*name_assignment*/));
      if (ps.token.tk != tk_rparen) {
        chomp_token(tk_comma);
      }
    }
    chomp_token(tk_rparen);
  }
  return new ctor_predicate_t(ctor_name.location, params, ctor_name,
                              name_assignment);
}

predicate_t *parse_tuple_predicate(parse_state_t &ps,
                                   maybe<identifier_t> name_assignment) {
  assert(ps.token.tk == tk_lparen);
  ps.advance();

  std::vector<predicate_t *> params;
  while (ps.token.tk != tk_rparen) {
    params.push_back(parse_predicate(
        ps, false /*allow_else*/, maybe<identifier_t>() /*name_assignment*/));
    if (ps.token.tk != tk_rparen || params.size() == 1) {
      chomp_token(tk_comma);
    }
  }
  chomp_token(tk_rparen);

  return new tuple_predicate_t(ps.token.location, params, name_assignment);
}

predicate_t *parse_predicate(parse_state_t &ps,
                             bool allow_else,
                             maybe<identifier_t> name_assignment) {
  if (ps.token.is_ident(K(else))) {
    if (!allow_else) {
      throw user_error(
          ps.token.location,
          "illegal keyword " c_type("%s") " in a pattern match context",
          ps.token.text.c_str());
    }
  } else if (is_restricted_var_name(ps.token.text)) {
    throw user_error(
        ps.token.location,
        "irrefutable predicates are restricted to non-keyword symbols");
  }

  if (ps.token.tk == tk_lparen) {
    return parse_tuple_predicate(ps, name_assignment);
  } else if (ps.token.tk == tk_identifier) {
    if (isupper(ps.token.text[0])) {
      /* match a ctor */
      return parse_ctor_predicate(ps, name_assignment);
    } else {
      if (name_assignment.valid) {
        throw user_error(
            ps.token.location,
            "pattern name assignment is only allowed once per term");
      } else {
        /* match anything */
        auto symbol = identifier_t::from_token(ps.token);
        ps.advance();
        if (ps.token.tk == tk_about) {
          ps.advance();

          return parse_predicate(ps, allow_else, maybe<identifier_t>(symbol));
        } else {
          return new irrefutable_predicate_t(symbol.location, symbol);
        }
      }
    }
  } else {
    if (name_assignment.valid) {
      throw user_error(ps.token.location,
                       "pattern name assignment is only allowed for data "
                       "constructor matching");
    }

    std::string sign;
    switch (ps.token.tk) {
    case tk_minus:
    case tk_plus:
      sign = ps.token.text;
      ps.advance();
      if (ps.token.tk != tk_integer && ps.token.tk != tk_float) {
        throw user_error(
            ps.prior_token.location,
            "unary prefix %s is not allowed before %s in this context",
            ps.prior_token.text.c_str(), ps.token.text.c_str());
      }
      break;
    default:
      break;
    }

    switch (ps.token.tk) {
    case tk_string:
    case tk_char: {
      /* match a literal */
      return new literal_t(ps.token_and_advance());
    }
    case tk_integer:
    case tk_float: {
      /* match a literal */
      predicate_t *literal = new literal_t(
          sign != ""
              ? Token(ps.token.location, ps.token.tk, sign + ps.token.text)
              : ps.token);
      ps.advance();
      return literal;
    }
    default:
      throw user_error(ps.token.location,
                       "unexpected token for pattern " c_warn("%s"),
                       ps.token.text.c_str());
    }
    return null_impl();
  }
}

pattern_block_t *parse_pattern_block(parse_state_t &ps) {
  return new pattern_block_t(
      parse_predicate(ps, true /*allow_else*/,
                      maybe<identifier_t>() /*name_assignment*/),
      parse_block(ps, false /*expression_means_return*/));
}

match_t *parse_match(parse_state_t &ps) {
  chomp_ident(K(match));
  bool auto_else = false;
  Token bang_token = ps.token;
  // TODO: this syntax needs some more thought. It's very subtle.
  if (ps.token.tk == tk_bang && ps.token.follows_after(ps.prior_token)) {
    auto_else = true;
    ps.advance();
  }
  auto scrutinee = parse_expr(ps);
  chomp_token(tk_lcurly);
  pattern_blocks_t pattern_blocks;
  while (ps.token.tk != tk_rcurly) {
    if (ps.token.is_ident(K(else))) {
      throw user_error(ps.token.location,
                       "place else patterns outside of the match block. (match "
                       "... { ... } else { ... })");
    }
    pattern_blocks.push_back(parse_pattern_block(ps));
  }
  chomp_token(tk_rcurly);
  if (auto_else) {
    auto pattern_block = new pattern_block_t(
        new irrefutable_predicate_t(bang_token.location, maybe<identifier_t>()),
        unit_expr(bang_token.location));
    pattern_blocks.push_back(pattern_block);
  }
  if (ps.token.is_ident(K(else))) {
    if (auto_else) {
      throw user_error(ps.token.location,
                       "no need for else block when you are using \"match!\". "
                       "either delete the ! or discard the else block");
    } else {
      pattern_blocks.push_back(parse_pattern_block(ps));
    }
  }

  if (pattern_blocks.size() == 0) {
    throw user_error(ps.token.location,
                     "when block did not have subsequent patterns to match");
  }

  return new match_t(scrutinee, pattern_blocks);
}

std::pair<identifier_t, types::type_t::ref> parse_lambda_param_core(
    parse_state_t &ps) {
  auto param_token = ps.token_and_advance();
  types::type_t::ref type;
  if (token_begins_type(ps.token)) {
    type = parse_type(ps);
  }

  return {iid(param_token), type};
}

std::pair<identifier_t, types::type_t::ref> parse_lambda_param(
    parse_state_t &ps) {
  if (ps.token.tk == tk_lparen) {
    ps.advance();
    if (ps.token.tk == tk_identifier) {
      return parse_lambda_param_core(ps);
    } else if (ps.token.tk == tk_rparen) {
      return {identifier_t{"_", ps.token.location},
              type_unit(ps.token.location)};
    }
  } else if (ps.token.tk == tk_comma) {
    ps.advance();
    if (ps.token.tk == tk_identifier) {
      return parse_lambda_param_core(ps);
    }
  }

  throw user_error(ps.token.location, "missing parameter name");
}

// TODO: put type mappings into the scope
expr_t *parse_lambda(parse_state_t &ps) {
  if (ps.token.tk == tk_identifier) {
    throw user_error(ps.token.location, "identifiers are unexpected here");
  }

  if (ps.token.tk == tk_lsquare) {
    throw user_error(ps.token.location, "not yet impl");
  }

  auto param = parse_lambda_param(ps);

  if (ps.token.tk == tk_comma) {
    return new lambda_t(param.first, param.second, nullptr,
                        new return_statement_t(parse_lambda(ps)));
  } else if (ps.token.tk == tk_rparen) {
    ps.advance();

    types::type_t::ref return_type;
    if (token_begins_type(ps.token) && !ps.line_broke()) {
      return_type = parse_type(ps);
    }
    return new lambda_t(param.first, param.second, return_type,
                        parse_block(ps, true /*expression_means_return*/));
  } else {
    throw user_error(ps.token.location, "unexpected token");
  }
}

types::type_t::ref parse_function_type(parse_state_t &ps) {
  chomp_token(tk_lparen);
  types::type_t::refs params;
  while (true) {
    if (ps.token.tk == tk_rparen) {
      ps.advance();
      break;
    }
    params.push_back(parse_type(ps));
    if (ps.token.tk == tk_comma) {
      ps.advance();
    }
  }

  if (params.size() == 0) {
    params.push_back(type_unit(ps.prior_token.location));
  }

  if (token_begins_type(ps.token) && !ps.line_broke()) {
    params.push_back(parse_type(ps));
  } else {
    params.push_back(type_unit(ps.prior_token.location));
  }

  return type_arrows(params);
}

types::type_t::ref parse_tuple_type(parse_state_t &ps) {
  chomp_token(tk_lparen);
  std::vector<types::type_t::ref> dims;
  bool is_tuple = false;
  while (true) {
    if (ps.token.tk == tk_rparen) {
      if (dims.size() == 0) {
        is_tuple = true;
      }
      ps.advance();
      break;
    }

    dims.push_back(parse_type(ps));
    if (ps.token.tk == tk_comma) {
      ps.advance();
      is_tuple = true;
    }
  }

  if (is_tuple) {
    if (dims.size() != 0) {
      return type_tuple(dims);
    } else {
      return type_unit(ps.token.location);
    }
  } else {
    return dims[0];
  }
}

types::type_t::ref parse_square_type(parse_state_t &ps) {
  auto location = ps.token.location;
  chomp_token(tk_lsquare);
  auto lhs = parse_type(ps);
  if (ps.token.tk == tk_colon) {
    ps.advance();
    auto rhs = parse_type(ps);
    chomp_token(tk_rsquare);
    return type_map(lhs, rhs);
  } else {
    chomp_token(tk_rsquare);
    return type_operator(type_id(identifier_t{VECTOR_TYPE, location}), lhs);
  }
}

types::type_t::ref parse_named_type(parse_state_t &ps) {
  if (islower(ps.token.text[0])) {
    return type_variable(iid(ps.token_and_advance()));
  } else {
    return type_id(ps.identifier_and_advance());
  }
}

types::type_t::ref parse_type(parse_state_t &ps) {
  /* look for type application */
  std::vector<types::type_t::ref> types;
  if (ps.line_broke()) {
    throw user_error(
        ps.token.location,
        "encountered a line break where a type annotation was expected");
  }

  while (token_begins_type(ps.token) && !ps.line_broke()) {
    if (ps.token.tk == tk_lparen) {
      types.push_back(parse_tuple_type(ps));
    } else if (ps.token.tk == tk_lsquare) {
      types.push_back(parse_square_type(ps));
    } else if (ps.token.is_ident(K(fn))) {
      ps.advance();
      types.push_back(parse_function_type(ps));
    } else if (ps.token.tk == tk_identifier) {
      types.push_back(parse_named_type(ps));
    } else if (ps.token.tk == tk_times) {
      types.push_back(type_id(
          identifier_t{PTR_TYPE_OPERATOR, ps.token_and_advance().location}));
    } else {
      auto error = user_error(ps.token.location,
                              "unhandled syntax for type specification");
      error.add_info(ps.token.location, "type components found so far: [%s]",
                     join_str(types, ", ").c_str());
      throw error;
    }
  }
  if (types.size() == 0) {
    throw user_error(ps.token.location, "expected a type here");
  } else if (types.size() == 1) {
    return types[0];
  } else {
    return type_operator(types);
  }
}

type_decl_t parse_type_decl(parse_state_t &ps) {
  expect_token(tk_identifier);

  auto class_id = iid(ps.token_and_advance());
  if (!isupper(class_id.name[0])) {
    throw user_error(
        class_id.location,
        "names in type-space must begin with an upper-case letter");
  }

  std::vector<identifier_t> params;
  while (!ps.line_broke()) {
    if (ps.token.tk == tk_identifier) {
      if (!islower(ps.token.text[0])) {
        throw user_error(ps.token.location,
                         "type declaration parameters must be lowercase");
      }
      params.push_back(iid(ps.token_and_advance()));
    } else {
      break;
    }
  }
  return type_decl_t{class_id, params};
}

types::type_t::ref create_ctor_type(location_t location,
                                    const type_decl_t &type_decl,
                                    types::type_t::refs param_types) {
  /* push the return type on as the final type */
  param_types.push_back(type_decl.get_type());
  auto type = type_arrows(param_types);

  for (int i = type_decl.params.size() - 1; i >= 0; --i) {
    type = type_lambda(type_decl.params[i], type);
  }
  return type;
}

expr_t *create_ctor(location_t location,
                    int ctor_id,
                    const type_decl_t &type_decl,
                    types::type_t::refs param_types) {
  std::vector<expr_t *> dims;
  /* add the ctor's id value as the first element in the tuple */
  dims.push_back(
      new literal_t({location, tk_integer, string_format("%d", ctor_id)}));

  std::vector<identifier_t> params;
  for (int i = 0; i < param_types.size(); ++i) {
    /* enumerate the nested lambda variables */
    params.push_back(identifier_t{fresh(), param_types[i]->get_location()});
    dims.push_back(new var_t(params.back()));
  }

  expr_t *expr = new as_t(new tuple_t(location, dims),
                          scheme({}, {}, type_decl.get_type()),
                          true /*force_cast*/);

  assert(dims.size() == params.size() + 1);
  for (int i = params.size() - 1; i >= 0; --i) {
    /* (λx y z . return! (ctor_id, x, y, z) as! type_decl) */
    expr = new lambda_t(params[i], param_types[i], nullptr,
                        new return_statement_t(expr));
  }

  return expr;
}

struct data_type_decl_t {
  type_decl_t type_decl;
  std::vector<decl_t *> decls;
};

data_type_decl_t parse_struct_decl(parse_state_t &ps,
                                   types::type_t::map &data_ctors) {
  type_decl_t type_decl = parse_type_decl(ps);
  std::vector<decl_t *> decls;
  types::type_t::refs dims;
  identifiers_t member_ids;

  member_ids.push_back(make_iid("ctor_id"));
  /* ctor_id */
  dims.push_back(type_id(make_iid(INT_TYPE)));

  chomp_token(tk_lcurly);
  for (int i = 0; true; ++i) {
    if (ps.token.tk == tk_rcurly) {
      break;
    }
    expect_token(tk_identifier);

    if (!islower(ps.token.text[0])) {
      throw user_error(ps.token.location,
                       "struct members must begin with lowercase letters");
    }

    member_ids.push_back(iid(ps.token_and_advance()));
    dims.push_back(parse_type(ps));
  }

  for (int i = 1 /*skip the ctor_id*/; i < dims.size(); ++i) {
    decls.push_back(new decl_t(
        /* accessor function names look like __.x */
        make_accessor_id(member_ids[i]),
        new lambda_t(
            identifier_t{"obj", member_ids[i].location}, type_decl.get_type(),
            dims[i],
            new return_statement_t(new tuple_deref_t(
                new as_t(new var_t(identifier_t{"obj", member_ids[i].location}),
                         scheme({}, {}, type_tuple(dims)), true /*force_cast*/),
                i, dims.size())))));
  }

  chomp_token(tk_rcurly);

  /* we don't need the ctor_id below */
  dims = vec_slice(dims, 1, dims.size());

  /* there is only one ctor for structs which are just product types */
  auto ctor_id = type_decl.id;

  data_ctors[ctor_id.name] = create_ctor_type(ctor_id.location, type_decl,
                                              dims);
  decls.push_back(
      new decl_t(ctor_id, create_ctor(ctor_id.location,
                                      // TODO: replace this construct with a
                                      // newtype tuple once newtype exists.
                                      0 /*ctor_id*/, type_decl, dims)));
  // log("parsed struct with decls %s", join_str(decls, ", ").c_str());

  return {type_decl, decls};
}

data_type_decl_t parse_newtype_decl(parse_state_t &ps,
                                    types::type_t::map &data_ctors,
                                    ctor_id_map_t &ctor_id_map) {
  expect_token(tk_identifier);
  Token type_name = ps.token;
  type_decl_t type_decl = parse_type_decl(ps);

  chomp_token(tk_assign);
  if (ps.token.tk != tk_identifier || ps.token.text != type_name.text) {
    throw user_error(ps.token.location,
                     "newtype must have a single constructor whose name "
                     "matches the type name");
  }

  chomp_token(tk_identifier);
  types::type_t::ref rhs_type = parse_type(ps);

  decl_t *decl;
  std::vector<types::type_t::ref> ctor_parts;
  if (auto tuple_type = dyncast<const types::type_tuple_t>(rhs_type)) {
    debug_above(3, log("build decl for tuple newtype ctor :: " c_id("%s") "%s",
                       type_name.text.c_str(), tuple_type->str().c_str()));
    ctor_parts = tuple_type->dimensions;
    std::vector<identifier_t> dim_names;
    std::vector<expr_t *> dims;
    for (int i = 0; i < tuple_type->dimensions.size(); ++i) {
      dim_names.push_back(identifier_t{
          bitter::fresh(), tuple_type->dimensions[i]->get_location()});
      dims.push_back(new var_t(dim_names.back()));
    }

    /* the inner part of the newtype ctor */
    expr_t *body = new as_t(new tuple_t(tuple_type->get_location(), dims),
                            type_decl.get_type()->generalize({}),
                            true /*force_cast*/);
    int i = tuple_type->dimensions.size();
    for (auto type_iter = tuple_type->dimensions.rbegin();
         type_iter != tuple_type->dimensions.rend(); ++type_iter) {
      body = new lambda_t(dim_names[--i], *type_iter,
                          type_variable(INTERNAL_LOC()),
                          new return_statement_t(body));
    }
    decl = new decl_t(type_decl.id, body);
  } else {
    ctor_parts.push_back(rhs_type);
    identifier_t param_iid = identifier_t{bitter::fresh(),
                                          rhs_type->get_location()};
    decl = new decl_t(type_decl.id,
                      new lambda_t(param_iid, rhs_type, type_decl.get_type(),
                                   new return_statement_t(new as_t(
                                       new var_t(param_iid),
                                       type_decl.get_type()->generalize({}),
                                       true /*force_cast*/))));
  }
  data_ctors[type_decl.id.name] = create_ctor_type(type_decl.id.location,
                                                   type_decl, ctor_parts);
  assert(!in(type_decl.id.name, ps.type_env));
  /* because this is a newtype, we need to remember the type mapping within the
   * type environment for reference later in pattern matching, and in code
   * generation. */
  types::type_t::ref body = rhs_type;
  for (auto param : type_decl.params) {
    body = type_lambda(param, body);
  }
  debug_above(4, log_location(type_decl.id.location,
                              "adding %s to the type_env as %s",
                              type_decl.id.name.c_str(), body->str().c_str()));
  ps.type_env[type_decl.id.name] = body;
  return {type_decl, {decl}};
}

data_type_decl_t parse_data_type_decl(parse_state_t &ps,
                                      types::type_t::map &data_ctors,
                                      ctor_id_map_t &ctor_id_map) {
  type_decl_t type_decl = parse_type_decl(ps);

  chomp_token(tk_lcurly);
  struct data_ctor_parts_t {
    Token ctor_token;
    types::type_t::refs param_types;
  };
  std::list<std::unique_ptr<data_ctor_parts_t>> data_ctors_parts;

  size_t param_types_count = 0;
  while (true) {
    expect_token(tk_identifier);

    std::unique_ptr<data_ctor_parts_t> data_ctor_parts =
        std::make_unique<data_ctor_parts_t>();
    data_ctor_parts->ctor_token = ps.token_and_advance();
    if (ps.token.tk == tk_lparen) {
      ps.advance();
      /* this is a data ctor */
      while (true) {
        /* parse the types of the dimensions (unnamed for now) */
        data_ctor_parts->param_types.push_back(parse_type(ps));
        /* keep track of whether any of the values in this data type require
         * extra storage. NB: Bool only being 1 word in size relies on this. */
        if (ps.token.tk == tk_comma) {
          ps.advance();
        } else {
          chomp_token(tk_rparen);
          break;
        }
      }
      param_types_count += data_ctor_parts->param_types.size();
    } else {
      /* this is a constant (like an enum) */
    }

    data_ctors_parts.emplace_back(std::move(data_ctor_parts));

    if (ps.token.tk == tk_rcurly) {
      ps.advance();
      break;
    }
  }

  std::vector<decl_t *> decls;
  if (param_types_count == 0) {
    /* this is just an ENUM. this type can be simplified to just an Int */
    ps.type_env[type_decl.id.name] = type_id(make_iid(INT_TYPE));

    /* build the decls for the various values */
    int i = 0;
    for (auto &data_ctor_parts : data_ctors_parts) {
      auto ctor_id = iid(data_ctor_parts->ctor_token);
      debug_above(3, log_location(ctor_id.location, "creating enum type for %s",
                                  ctor_id.str().c_str()));
      data_ctors[ctor_id.name] = type_decl.get_type();
      decls.push_back(new decl_t(
          ctor_id,
          new as_t(new literal_t(
                       Token{ctor_id.location, tk_integer, std::to_string(i)}),
                   type_decl.get_type()->generalize({}), true /*force_cast*/)));
      ctor_id_map[ctor_id.name] = i++;
    }
  } else {
    int i = 0;
    for (auto &data_ctor_parts : data_ctors_parts) {
      auto ctor_id = iid(data_ctor_parts->ctor_token);
      debug_above(3, log_location(ctor_id.location,
                                  "creating constructor type for %s",
                                  ctor_id.str().c_str()));
      data_ctors[ctor_id.name] = create_ctor_type(ctor_id.location, type_decl,
                                                  data_ctor_parts->param_types);
      decls.push_back(
          new decl_t(ctor_id, create_ctor(ctor_id.location, i, type_decl,
                                          data_ctor_parts->param_types)));
      ctor_id_map[ctor_id.name] = i++;
    }
  }

  return {type_decl, decls};
}

instance_t *parse_type_class_instance(parse_state_t &ps) {
  identifier_t type_class_id = ps.identifier_and_advance();
  types::type_t::ref type = parse_type(ps);
  chomp_token(tk_lcurly);

  std::vector<decl_t *> decls;
  while (true) {
    if (ps.token.is_ident(K(fn))) {
      /* instance-level functions */
      ps.advance();
      auto token = ps.token_and_advance();
      auto id = ps.id_mapped(identifier_t{token.text, token.location});
      decls.push_back(new decl_t(id, parse_lambda(ps)));
    } else if (ps.token.tk != tk_rcurly) {
      /* instance-level let vars */
      auto name_token = ps.token_and_advance();
      auto id = ps.id_mapped(
          identifier_t{name_token.text, name_token.location});
      chomp_token(tk_assign);
      decls.push_back(new decl_t(id, parse_expr(ps)));
    } else {
      chomp_token(tk_rcurly);
      break;
    }
  }

  return new instance_t{type_class_id, type, decls};
}

type_class_t *parse_type_class(parse_state_t &ps) {
  type_decl_t type_decl = parse_type_decl(ps);

  if (type_decl.params.size() != 1) {
    throw user_error(
        type_decl.id.location,
        "type classes must be parameterized over (only) one type variable");
  }

  chomp_token(tk_lcurly);
  std::set<std::string> superclasses;
  types::type_t::map overloads;
  while (true) {
    if (ps.token.is_ident(K(has))) {
      ps.advance();
      expect_token(tk_identifier);
      if (!isupper(ps.token.text[0])) {
        throw user_error(ps.token.location,
                         "type class requirements need to be upper-case "
                         "because type classes need to be uppercase");
      }
      if (in(ps.token.text, superclasses)) {
        throw user_error(ps.token.location,
                         "type class requirement mentioned more than once");
      }
      superclasses.insert(ps.identifier_and_advance().name);
    } else if (ps.token.is_ident(K(fn))) {
      /* an overloaded function */
      ps.advance();
      auto id = identifier_t{ps.token.text, ps.token.location};
      ps.advance();

      /*
      auto predicates = superclasses;
      predicates.insert(type_decl.id.name);

      types::type_t::map bindings;
      bindings[type_decl.params[0].name] =
      type_variable(gensym(type_decl.params[0].location), predicates);
      */
      overloads[id.name] = parse_function_type(
          ps); // ->rebind(bindings)->generalize({})->normalize();
    } else {
      chomp_token(tk_rcurly);
      break;
    }
  }

  return new type_class_t(type_decl.id, type_decl.params[0], superclasses,
                          overloads);
}

module_t *parse_module(parse_state_t &ps,
                       std::vector<module_t *> auto_import_modules,
                       std::set<identifier_t> &module_deps) {
  debug_above(6, log("about to parse %s", ps.filename.c_str()));

  for (auto aim : auto_import_modules) {
    if (aim == nullptr) {
      continue;
    }
    std::set<std::string> tlds = compiler::get_top_level_decls(
        aim->decls, aim->type_decls, aim->type_classes);
    for (auto tld : tlds) {
      debug_above(9, log("adding tld %s -> %s in %s", tld.c_str(),
                         (aim->name + "." + tld).c_str(), ps.filename.c_str()));
      ps.add_term_map(INTERNAL_LOC(), tld, aim->name + "." + tld);
    }
  }

  std::vector<decl_t *> decls;
  std::vector<type_decl_t> type_decls;
  std::vector<type_class_t *> type_classes;
  std::vector<instance_t *> instances;

  while (ps.token.is_ident(K(get))) {
    ps.advance();
    expect_token(tk_identifier);
    identifier_t module_name = ps.identifier_and_advance();

    if (ps.token.tk == tk_lcurly) {
      ps.advance();
      while (true) {
        expect_token(tk_identifier);
        if (starts_with(ps.token.text, "_")) {
          throw user_error(ps.token.location,
                           "it is not possible to import module-scoped "
                           "variables into other modules");
        }
        ps.add_term_map(ps.token.location, ps.token.text,
                        module_name.name + "." + ps.token.text);
        ps.advance();
        if (ps.token.tk == tk_comma) {
          ps.advance();
        } else {
          chomp_token(tk_rcurly);
          break;
        }
      }
    }
    module_deps.insert(module_name);
  }

  while (ps.token.is_ident(K(link))) {
    ps.advance();
    if (ps.token.is_ident(K(pkg))) {
      ps.advance();
      ps.link_ins.insert(LinkIn{lit_pkgconfig, ps.token_and_advance()});
    } else {
      throw user_error(ps.token.location, "unexpected link directive");
    }
  }

  while (true) {
    if (ps.token.is_ident(K(get))) {
      throw user_error(ps.token.location,
                       "get statements must occur at the top of the module");
    } else if (ps.token.is_ident(K(fn))) {
      /* module-level functions */
      ps.advance();
      auto id = identifier_t::from_token(ps.token_and_advance());
      decls.push_back(new decl_t(id, parse_lambda(ps)));
    } else if (ps.token.is_ident(K(struct))) {
      ps.advance();
      types::type_t::map data_ctors;
      auto data_type = parse_struct_decl(ps, data_ctors);
      type_decls.push_back(data_type.type_decl);
      for (auto &decl : data_type.decls) {
        decls.push_back(decl);
      }
      ps.data_ctors_map[data_type.type_decl.id.name] = data_ctors;
    } else if (ps.token.is_ident(K(newtype))) {
      /* module-level newtypes */
      ps.advance();
      types::type_t::map data_ctors;
      data_type_decl_t data_type = parse_newtype_decl(ps, data_ctors,
                                                      ps.ctor_id_map);
      type_decls.push_back(data_type.type_decl);
      for (auto &decl : data_type.decls) {
        decls.push_back(decl);
      }
      ps.data_ctors_map[data_type.type_decl.id.name] = data_ctors;
    } else if (ps.token.is_ident(K(data))) {
      /* module-level data types */
      ps.advance();
      types::type_t::map data_ctors;
      data_type_decl_t data_type = parse_data_type_decl(ps, data_ctors,
                                                        ps.ctor_id_map);
      type_decls.push_back(data_type.type_decl);
      for (auto &decl : data_type.decls) {
        decls.push_back(decl);
      }
      ps.data_ctors_map[data_type.type_decl.id.name] = data_ctors;
    } else if (ps.token.is_ident(K(let))) {
      /* module-level constants */
      ps.advance();
      auto id = identifier_t::from_token(ps.token_and_advance());
      chomp_token(tk_assign);
      decls.push_back(new decl_t(id, parse_expr(ps)));
    } else if (ps.token.is_ident(K(class))) {
      /* module-level type classes */
      ps.advance();
      type_classes.push_back(parse_type_class(ps));
    } else if (ps.token.is_ident(K(instance))) {
      /* module-level type instances */
      ps.advance();
      instances.push_back(parse_type_class_instance(ps));
    } else {
      break;
    }
  }
  if (ps.token.tk != tk_none) {
    throw user_error(ps.token.location, "unknown stuff here");
  }
  return new module_t(ps.module_name, decls, type_decls, type_classes,
                      instances, ps.ctor_id_map, ps.data_ctors_map,
                      ps.type_env);
}
