#include "parser.h"

#include <csignal>
#include <iostream>
#include <stdlib.h>
#include <string>

#include "ast.h"
#include "builtins.h"
#include "class_predicate.h"
#include "compiler.h"
#include "disk.h"
#include "host.h"
#include "logger.h"
#include "logger_decls.h"
#include "parse_state.h"
#include "token.h"

namespace zion {
namespace parser {

using namespace ast;

class RawParseMode {
public:
  RawParseMode() = delete;
  RawParseMode(RawParseMode &) = delete;
  RawParseMode(ParseState &ps)
      : prior_sugar_literals(ps.sugar_literals), ps(ps) {
    ps.sugar_literals = false;
  }
  ~RawParseMode() {
    ps.sugar_literals = prior_sugar_literals;
  }

private:
  bool prior_sugar_literals;
  ParseState &ps;
};

Identifier make_accessor_id(Identifier id) {
  return Identifier{"__get_" + id.name, id.location};
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

const Expr *parse_var_decl(ParseState &ps,
                           bool is_let,
                           bool allow_destructuring) {
  if (ps.token.tk == tk_lparen ||
      (ps.token.tk == tk_identifier && isupper(ps.token.text[0]))) {
    /* this looks like a destructuring declaration */
    if (!allow_destructuring) {
      throw user_error(ps.token.location, "destructuring is not allowed here");
    }

    BoundVarLifetimeTracker bvlt(ps);
    auto prior_token = ps.token;
    const Predicate *predicate = parse_predicate(ps, false /*allow_else*/,
                                                 maybe<Identifier>());
    chomp_token(tk_assign);
    /* parse the initializer in the context before the left-hand side */
    const Expr *rhs = bvlt.escaped_parse_expr(
        false /*allow_for_comprehensions*/);
    const Expr *body = parse_block(ps, false /*expression_means_return*/);
    return new Match(rhs, {new PatternBlock(predicate, body)});
  } else {
    expect_token(tk_identifier);
    assert(!isupper(ps.token.text[0]));
    return parse_let(ps, iid(ps.token_and_advance()), is_let);
  }
}

const Expr *parse_let(ParseState &ps, Identifier var_id, bool is_let) {
  auto location = ps.token.location;
  const Expr *initializer = nullptr;

  if (!ps.line_broke() && (ps.token.tk == tk_assign)) {
    ps.advance();
    initializer = parse_expr(ps, false /*allow_for_comprehensions*/);
  } else {
    initializer = new Application(
        new Var(ps.id_mapped(Identifier{"new", location})),
        unit_expr(INTERNAL_LOC()));
  }

  BoundVarLifetimeTracker bvlt(ps);

  ps.term_map.erase(var_id.name);

  if (!is_let) {
    auto ref_id = Identifier{"std.Ref", location};
    initializer = new Application(new Var(ref_id), initializer);
    ps.mutable_vars.insert(var_id.name);
  } else {
    ps.mutable_vars.erase(var_id.name);
  }

  return new Let(var_id, initializer,
                 parse_block(ps, false /*expression_means_return*/));
}

const Expr *parse_return_statement(ParseState &ps) {
  auto return_token = ps.token;
  chomp_ident(K(return ));
  return new ReturnStatement(
      (!ps.line_broke() && ps.token.tk != tk_rcurly)
          ? parse_expr(ps, false /*allow_for_comprehensions*/)
          : unit_expr(INTERNAL_LOC()));
}

const Expr *parse_for_block(ParseState &ps) {
  chomp_ident(K(for));

  bool filtered_matching = ps.token.is_ident(K(match));
  if (filtered_matching) {
    ps.advance();
  }

  BoundVarLifetimeTracker bvlt(ps);
  const Predicate *for_var_predicate = parse_predicate(
      ps, false /* allow_else */, maybe<Identifier>(), true /*allow_var_refs*/);

  auto in_token = ps.token;
  chomp_ident(K(in));

  const Expr *iterable = bvlt.escaped_parse_expr(
      false /*allow_for_comprehensions*/);
  const Expr *block = parse_block(ps, false /*expression_means_return*/);

  auto iterator_id = Identifier{fresh(), for_var_predicate->get_location()};
  PatternBlocks pattern_blocks;
  pattern_blocks.push_back(new PatternBlock(
      new CtorPredicate(iterator_id.location, {for_var_predicate},
                        ps.id_mapped(Identifier{"Just", iterator_id.location}),
                        maybe<Identifier>()),
      block));

  if (filtered_matching) {
    /* allow for loops to only handle matching patterns */
    pattern_blocks.push_back(new PatternBlock(
        new CtorPredicate(
            iterator_id.location,
            {new IrrefutablePredicate(
                iterator_id.location,
                Identifier{fresh(), iterator_id.location})},
            ps.id_mapped(Identifier{"Just", iterator_id.location}),
            maybe<Identifier>()),
        new Continue(in_token.location)));
  }

  pattern_blocks.push_back(new PatternBlock(
      new CtorPredicate(
          iterator_id.location, {},
          ps.id_mapped(Identifier{"Nothing", iterator_id.location}),
          maybe<Identifier>()),
      new Break(in_token.location)));

  return new Let(
      iterator_id,
      new Application(
          new Var(ps.id_mapped(Identifier{"iter", in_token.location})),
          iterable),
      new While(new Var(ps.id_mapped(Identifier{"True", in_token.location})),
                new Match(new Application(new Var(iterator_id),
                                          unit_expr(iterator_id.location)),
                          pattern_blocks)));
}

const Expr *parse_new_expr(ParseState &ps) {
  ps.advance();
  return new As(new Application(new Var(ps.id_mapped(Identifier{
                                    "new", ps.prior_token.location})),
                                unit_expr(ps.token.location)),
                parse_type(ps, true /*allow_top_level_application*/),
                false /*force_cast*/);
}

const Expr *parse_static_print(ParseState &ps) {
  auto location = ps.token_and_advance().location;
  chomp_token(tk_lparen);
  auto sp = new StaticPrint(location,
                            parse_expr(ps, true /*allow_for_comprehensions*/));
  chomp_token(tk_rparen);
  return sp;
}

// assert macro expansion. should avoid lib/std for
const Expr *parse_assert(ParseState &ps) {
  Token assert_token = ps.token;
  chomp_ident(K(assert));
  chomp_token(tk_lparen);

  const Expr *condition = parse_expr(ps, false /*allow_for_comprehensions*/);
  std::string assert_message = string_format(
      "%s: assertion failed: (%s)\n", ps.token.location.repr().c_str(),
      clean_ansi_escapes(condition->str()).c_str());
  const Expr *assertion = new Conditional(
      condition, // The condition we are asserting
      unit_expr(ps.token.location),
      new Block({
          new As(new As(new Builtin(
                            new Var(Identifier{"__builtin_ffi_3",
                                               ps.token.location}),
                            {
                                new Literal(Token{ps.token.location, tk_string,
                                                  "\"write\""}),
                                new Literal(Token{ps.token.location, tk_integer,
                                                  "2" /*stderr*/}),
                                new Literal(
                                    Token{ps.token.location, tk_string,
                                          escape_json_quotes(assert_message)}),
                                new Literal(Token{
                                    ps.token.location, tk_integer,
                                    std::to_string(assert_message.size())}),
                            }),
                        type_id(make_iid(INT_TYPE)), false /*force_cast*/),
                 type_unit(INTERNAL_LOC()), true /*force_cast*/),
          new Builtin(
              new Var(make_iid("__builtin_ffi_1")),
              {new Literal(Token{assert_token.location, tk_string, "\"exit\""}),
               new Literal(Token{assert_token.location, tk_integer, "1"})}),
          unit_expr(ps.token.location),
      }));
  chomp_token(tk_rparen);
  return assertion;
}

const Expr *parse_defer(ParseState &ps) {
  chomp_ident(K(defer));
  const Expr *expr = parse_expr(ps, false /*allow_for_comprehensions*/);
  if (const Application *application = dcast<const Application *>(expr)) {
    return new Defer(application);
  } else {
    throw user_error(expr->get_location(),
                     "defer statements must be function callsites");
  }
  assert(false);
  return nullptr;
}

const Expr *parse_with(ParseState &ps) {
  chomp_ident(K(with));
  bool unwritten_else = false;
  if (ps.token.tk == tk_bang && ps.token.follows_after(ps.prior_token)) {
    unwritten_else = true;
    ps.advance();
  }

  const Predicate *predicate = nullptr;
  BoundVarLifetimeTracker bvlt(ps);

  if (ps.token.is_ident(K(let))) {
    ps.advance();
    predicate = parse_predicate(ps, false /*allow_else*/,
                                maybe<Identifier>() /*name_assignment*/);
    chomp_token(tk_assign);
  } else {
    predicate = new IrrefutablePredicate(
        ps.token.location, Identifier{fresh(), ps.token.location});
  }

  /* the context manager expression should be parsed in the prior context */
  const Expr *context_manager_expr = bvlt.escaped_parse_expr(
      false /*allow_for_comprehensions*/);
  /* parse the success block */
  const Expr *block = parse_block(ps, false /*expression_means_return*/);

  const Expr *else_block = unwritten_else ? unit_expr(ps.token.location)
                                          : nullptr;
  const Predicate *error_predicate = nullptr;
  if (unwritten_else || ps.token.is_ident(K(else))) {
    if (ps.token.is_ident(K(else)) && unwritten_else) {
      throw user_error(ps.token.location,
                       "prior 'with!' prevents the usage of 'else' here. "
                       "remove the 'else' or remove the '!'");
    }
    if (!unwritten_else) {
      ps.advance();
    }

    /* we are in a WithElseResource */
    /*
      match context_manager_expr {
        ResourceAcquired(resource, defer_closure) {
          defer defer_closure()
          block
        }
        ResourceFailure(error) {
          else block
        }
      }
    */
    if (!unwritten_else &&
        (ps.token.tk == tk_identifier || ps.token.tk == tk_lparen)) {
      error_predicate = parse_predicate(
          ps, false /*allow_else*/, maybe<Identifier>() /*name_assignment*/);
    } else {
      error_predicate = new IrrefutablePredicate(
          ps.token.location, Identifier{fresh(), ps.token.location});
    }
    if (!unwritten_else) {
      else_block = parse_block(ps, false /*expression_means_return*/);
    } else {
      assert(else_block != nullptr);
    }

    Identifier defer_id{fresh(), ps.token.location};
    /* construct the defer statement prior to the evaluation of the success
     * block */
    std::vector<const Expr *> statements = {
        new Defer(
            new Application(new Var(defer_id), unit_expr(defer_id.location))),
        block};
    return new Match(
        context_manager_expr,
        PatternBlocks{
            new PatternBlock(
                new CtorPredicate(
                    predicate->get_location(),
                    {new CtorPredicate(
                        predicate->get_location(),
                        {predicate,
                         new IrrefutablePredicate(defer_id.location, defer_id)},
                        Identifier{"std.WithResource",
                                   predicate->get_location()},
                        maybe<Identifier>{})},
                    Identifier{"std.ResourceAcquired",
                               predicate->get_location()},
                    maybe<Identifier>{}),
                new Block(statements)),
            new PatternBlock(
                new CtorPredicate(predicate->get_location(), {error_predicate},
                                  Identifier{"std.ResourceFailure",
                                             else_block->get_location()},
                                  maybe<Identifier>{}),
                else_block)});
  } else {
    Identifier defer_id{fresh(), ps.token.location};
    /* construct the defer statement prior to the evaluation of the block */
    std::vector<const Expr *> statements = {
        new Defer(
            new Application(new Var(defer_id), unit_expr(defer_id.location))),
        block};
    return new Match(
        context_manager_expr,
        PatternBlocks{new PatternBlock(
            new CtorPredicate(
                predicate->get_location(),
                {predicate,
                 new IrrefutablePredicate(defer_id.location, defer_id)},
                Identifier{"std.WithResource", predicate->get_location()},
                maybe<Identifier>{}),
            new Block(statements))});
  }
}

const Expr *parse_statement(ParseState &ps) {
  assert(ps.token.tk != tk_rcurly);

  if (ps.token.is_ident(K(var))) {
    ps.advance();
    return parse_var_decl(ps, false /*is_let*/, false /*allow_destructuring*/);
  } else if (ps.token.is_ident(K(let))) {
    ps.advance();
    return parse_var_decl(ps, true /*is_let*/, true /*allow_destructuring*/);
  } else if (ps.token.is_ident(K(if))) {
    return parse_if(ps);
  } else if (ps.token.is_ident(K(assert))) {
    return parse_assert(ps);
  } else if (ps.token.is_ident(K(defer))) {
    return parse_defer(ps);
  } else if (ps.token.is_ident(K(with))) {
    return parse_with(ps);
  } else if (ps.token.is_ident(K(while))) {
    return parse_while(ps);
  } else if (ps.token.is_ident(K(for))) {
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
      auto fn_id = Identifier::from_token(ps.token);

      BoundVarLifetimeTracker bvlt(ps);
      /* the function name is not a mutable value. */
      ps.mutable_vars.erase(fn_id.name);

      return new Let(fn_id, parse_lambda(ps),
                     parse_block(ps, false /*expression_means_return*/));
    } else {
      return parse_lambda(ps);
    }
  } else if (ps.token.is_ident(K(return ))) {
    return parse_return_statement(ps);
  } else if (ps.token.is_ident(K(unreachable))) {
    return new Var(iid(ps.token));
  } else if (ps.token.is_ident(K(continue))) {
    return new Continue(ps.token_and_advance().location);
  } else if (ps.token.is_ident(K(break))) {
    return new Break(ps.token_and_advance().location);
  } else {
    return parse_assignment(ps);
  }
}

const Expr *parse_var_ref(ParseState &ps) {
  if (ps.token.tk != tk_identifier) {
    throw user_error(ps.token.location, "expected an identifier");
  }

  if (ps.token.is_ident(K(__filename__))) {
    /* special case: inject the current filename as a raw string */
    auto token = ps.token_and_advance();
    return new Literal(Token{token.location, tk_string,
                             escape_json_quotes(token.location.filename)});
  } else if (in(ps.token.text, ps.builtin_arities)) {
    /* special case: this is a __builtin */
    RawParseMode rpm(ps);
    const int builtin_arity = get(ps.builtin_arities, ps.token.text, -1);

    int arity = builtin_arity;
    assert(arity >= 0);
    auto builtin_token = ps.token_and_advance();
    std::vector<const Expr *> exprs;
    if (arity > 0) {
      chomp_token(tk_lparen);
      while (true) {
        exprs.push_back(parse_expr(ps, false /*allow_for_comprehensions*/));
        --arity;
        if (arity > 0) {
          chomp_token(tk_comma);
        } else if (ps.token.tk == tk_rparen) {
          ps.advance();
          break;
        } else {
          throw user_error(ps.token.location,
                           "builtin %s only takes %d parameter%s",
                           builtin_token.str().c_str(), builtin_arity,
                           (builtin_arity == 1 ? "" : "s"));
        }
      }
    }

    return new Builtin(new Var(iid(builtin_token)), exprs);
  } else if (ps.token.text == "__host_int") {
    /* special case: __host_int is another special-case of builtin to access
     * target host OS constants */
    RawParseMode rpm(ps);
    ps.advance();
    chomp_token(tk_lparen);
    expect_token(tk_identifier);
    Location location = ps.token.location;
    int value = get_host_int(location, ps.token_and_advance().text);
    chomp_token(tk_rparen);
    return new Literal(Token{location, tk_integer, std::to_string(value)});
  } else if (ps.token.is_ident(K(if))) {
    /* special case: helpful error */
    throw user_error(ps.token.location,
                     "if statements cannot be used as expressions. use the "
                     "ternary operator ?:");
  } else if (ps.token.is_ident(K(while))) {
    /* special case: helpful error */
    throw user_error(ps.token.location,
                     "%s statements cannot be used as expressions",
                     ps.token.text.c_str());
  }

  /* normal case of a variable reference */
  bool allow_automatic_dereferencing = true;
  if (ps.token.is_ident(K(var))) {
    ps.advance();
    allow_automatic_dereferencing = false;
  }

  auto id = ps.identifier_and_advance();
  const Expr *var_ref = new Var(id);
  if (!allow_automatic_dereferencing) {
    return var_ref;
  } else if (is_assignment_operator(ps.token.tk) ||
             ps.mutable_vars.count(id.name) == 0) {
    /* this is just a regular variable reference, we don't know if its a "Ref a"
     * type or not, but we suspect not, based on the lexicographical information
     * available */
    return var_ref;
  } else {
    /* we know that this was declared as a "var", so let's just automatically
     * load it for the user. if they want to treat is as a ref, then they need
     * to preface it with & */
    return new Application(new Var(Identifier{"std.load_value", id.location}),
                           var_ref);
  }
}

const Expr *parse_base_expr(ParseState &ps) {
  if (ps.token.tk == tk_lparen) {
    return parse_tuple_expr(ps);
  } else if (ps.token.is_ident(K(new))) {
    return parse_new_expr(ps);
  } else if (ps.token.is_ident(K(fn))) {
    ps.advance();
    return parse_lambda(ps);
  } else if (ps.token.tk == tk_pipe) {
    return parse_lambda(ps, tk_pipe, tk_pipe);
  } else if (ps.token.is_ident(K(match))) {
    return parse_match(ps);
  } else if (ps.token.is_ident(K(null))) {
    return new As(
        new Literal(Token{ps.token_and_advance().location, tk_integer, "0"}),
        type_ptr(type_variable(ps.prior_token.location)), true /*force_cast*/);
  } else if (ps.token.tk == tk_identifier) {
    return parse_var_ref(ps);
  } else {
    return parse_literal(ps);
  }
}

const Expr *build_array_literal(Location location,
                                const std::vector<const Expr *> &exprs) {
  auto array_var = new Var(Identifier(fresh(), location));

  /* take all the exprs from the array, and turn them into statements to fill
   * out a vector */
  std::vector<const Expr *> stmts;

  if (exprs.size() != 0) {
    /* we know how many items we'll need space for, so we might as well reserve
     * that space ahead of time */
    stmts.push_back(
        new Application(new Var(Identifier{"std.reserve", location}),
                        {new Var(array_var->id),
                         new Literal(Token{location, tk_integer,
                                           std::to_string(exprs.size())})}));
  }

  for (auto expr : exprs) {
    stmts.push_back(
        new Application(new Var(Identifier{"std.append", expr->get_location()}),
                        {array_var, expr}));
  }

  /* now, add another item just for the actual array value to be returned */
  stmts.push_back(array_var);

  return new Let(
      array_var->id,
      new As(new Application(new Var(Identifier{"std.new", location}),
                             unit_expr(location)),
             type_vector_type(type_variable(location)), false /*force_cast*/),
      new Block(stmts));
}

struct GeneratorFor {
  const Predicate *predicate = nullptr;
  const Expr *iterable = nullptr;
  const Expr *condition = nullptr;
};

GeneratorFor parse_generator_for(ParseState &ps) {
  auto for_token = ps.token;
  chomp_ident(K(for));
  GeneratorFor generator_for;
  generator_for.predicate = parse_predicate(
      ps, false /*allow_else*/, maybe<Identifier>() /*name_assignment*/);
  chomp_ident(K(in));
  generator_for.iterable = parse_expr(ps, false /*allow_for_comprehensions*/);
  if (ps.token.is_ident(K(if))) {
    ps.advance();
    generator_for.condition = parse_expr(ps,
                                         false /*allow_for_comprehensions*/);
  }
  return generator_for;
}

//
// (expr
//    for predicate_1 in iterable_1 if cond_1
//    for predicate_2 in iterable_2 if cond_2
//    ...)
//
// expands to:
// {
//   let i_1 = iter(iterable_1),
//   fn () {
//     while True {
//       match i_1() {
//         Just(predicate_1) {
//           if cond_1 {
//             recurse for _2...
//               return Just(expr)
//           }
//         }
//         Nothing {
//           return Nothing
//         }
//       }
//     }
//   }
// }
//
// [expr for predicate_1 in iterable if cond]
// expands to:
// vector(...the above...)
//
const Expr *build_generator(Location location,
                            const Expr *expr,
                            const GeneratorFor &generator_for) {
  Identifier iterator_id{fresh(), location};
  return new Let(
      iterator_id,
      new Application(new Var(Identifier{"std.iter", location}),
                      generator_for.iterable),
      new Lambda(
          {Identifier{fresh(), location}}, {type_unit(location)},
          type_operator(type_id(Identifier{MAYBE_TYPE, expr->get_location()}),
                        type_variable(expr->get_location())),
          new Block(
              {new While(
                   new Var(Identifier{"std.True", location}),
                   new Match(
                       new Application(new Var(iterator_id),
                                       unit_expr(location)),
                       {new PatternBlock(
                            new CtorPredicate(
                                location, {generator_for.predicate},
                                Identifier{"maybe.Just", location},
                                maybe<Identifier>()),
                            generator_for.condition != nullptr
                                ? static_cast<const Expr *>(new Conditional(
                                      generator_for.condition,
                                      new ReturnStatement(new Application(
                                          new Var(Identifier{"maybe.Just",
                                                             location}),
                                          expr)),
                                      unit_expr(location)))
                                : static_cast<const Expr *>(
                                      new ReturnStatement(new Application(
                                          new Var(Identifier{"maybe.Just",
                                                             location}),
                                          expr)))),
                        new PatternBlock(new CtorPredicate(location, {},
                                                           Identifier{"std."
                                                                      "Nothing",
                                                                      location},
                                                           maybe<Identifier>()),
                                         new ReturnStatement(new Var(Identifier{
                                             "maybe.Nothing", location})))})),
               new ReturnStatement(
                   new Var(Identifier{"maybe.Nothing", location}))})));
}

const Expr *parse_generator(ParseState &ps, const Expr *expr) {
  BoundVarLifetimeTracker bvlt(ps);
  GeneratorFor generator_for = parse_generator_for(ps);
  if (ps.token.is_ident(K(for))) {
    throw user_error(ps.token.location,
                     "nested comprehensions are not legal in Zion");
  }
  return build_generator(expr->get_location(), expr, generator_for);
}

const Expr *parse_array_literal(ParseState &ps) {
  Location location = ps.token.location;
  chomp_token(tk_lsquare);
  std::vector<const Expr *> exprs;

  int i = 0;
  while (ps.token.tk != tk_rsquare && ps.token.tk != tk_none) {
    ++i;
    exprs.push_back(parse_expr(ps, false /*allow_for_comprehensions*/));

    if (ps.token.tk == tk_double_dot && (i == 1 || i == 2)) {
      /* range syntax with step calculation */
      auto location = ps.token.location;
      ps.advance();
      /* let range_min = exprs[0] in let range_next = {exprs[1] or
       * (range_min+1)} in let range_max = {exprs[1] or Max Int} in
       * Range(range_min, range_next-range_min, range_max) */

      Identifier range_min = Identifier("__range_min" + fresh(),
                                        ps.token.location);
      Identifier range_next = Identifier("__range_next" + fresh(),
                                         ps.token.location);
      Identifier range_max = Identifier("__range_max" + fresh(),
                                        ps.token.location);

      auto range_body = new Application(
          new Var(ps.id_mapped(Identifier{"Range", location})),
          {new Var(range_min),
           new Application(new Var(Identifier("std.-", ps.token.location)),
                           {new Var(range_next), new Var(range_min)}),
           new Var(range_max)});

      auto let_range_max = new Let(
          range_max,
          (ps.token.tk != tk_rsquare)
              ? parse_expr(ps, false /*allow_for_comprehensions*/)
              : new Application(
                    new Var(Identifier{"math.max_bound", ps.token.location}),
                    unit_expr(location)),
          range_body);

      auto let_range_next = new Let(
          range_next,
          (i == 2)
              ? exprs[1]
              : new Application(new Var(make_iid("std.+")),
                                {new Literal(Token{location, tk_integer, "1"}),
                                 new Var(range_min)}),
          let_range_max);

      auto let_range_min = new Let(range_min, exprs[0], let_range_next);

      chomp_token(tk_rsquare);
      return let_range_min;
    } else if (ps.token.tk == tk_comma) {
      ps.advance();
    } else if (ps.token.is_ident(K(for))) {
      if (i == 1) {
        const Expr *list_comprehension = new Application(
            new Var(Identifier{"std.vector", location}),
            parse_generator(ps, exprs[0]));
        chomp_token(tk_rsquare);
        return list_comprehension;

      } else {
        // TODO: consider allowing this as a special case
        throw user_error(
            ps.token.location,
            "for comprehensions are not allowed after multiple array elements");
      }
    } else if (ps.token.tk != tk_rsquare) {
      throw user_error(
          ps.token.location,
          "found something (%s) that does not make sense in an array literal",
          ps.token.str().c_str());
    }
  }
  chomp_token(tk_rsquare);

  return build_array_literal(location, exprs);
}

const Expr *parse_string_literal(const Token &token) {
  assert(token.tk == tk_string);
  std::string str = unescape_json_quotes(token.text);
  int string_len = str.size();
  return new Application(
      new Var(Identifier{STRING_TYPE, token.location}),
      {new Literal(token), new Literal(Token{token.location, tk_integer,
                                             std::to_string(string_len)})});
}

const Expr *parse_string_expr_prefix(const Token &token) {
  if (token.text.size() > 3 /* "${ is the minimal string_expr_prefix */) {
    Token token_fixed = Token(token.location, tk_string,
                              token.text.substr(0, token.text.size() - 2) +
                                  "\"");
    return parse_string_literal(token_fixed);
  } else {
    return nullptr;
  }
}

const Expr *parse_string_expr_continuation(const Token &token) {
  assert(token.text.size() >= 3);
  assert(token.text[0] == '}');
  assert(token.text[token.text.size() - 2] == '$');
  assert(token.text[token.text.size() - 1] == '{');

  if (token.text.size() > 3 /* }${ is the minimal string_expr_continuation */) {
    Token token_fixed = Token(
        token.location, tk_string,
        "\"" + token.text.substr(1, token.text.size() - 3) + "\"");
    return parse_string_literal(token_fixed);
  } else {
    return nullptr;
  }
}

const Expr *parse_string_expr_suffix(const Token &token) {
  assert(token.text.size() >= 2);
  assert(token.text[0] == '}');
  assert(token.text[token.text.size() - 1] == '\"');

  if (token.text.size() > 2 /* }" is the minimal string_expr_suffix */) {
    Token token_fixed = Token(token.location, tk_string,
                              "\"" +
                                  token.text.substr(1, token.text.size() - 1));
    return parse_string_literal(token_fixed);
  } else {
    return nullptr;
  }
}

const Expr *parse_string_expr(ParseState &ps) {
  /* parse string interpolation like "x = ${x}, y = ${y}, x + y = ${x + y}" */
  assert(ps.token.tk == tk_string_expr_prefix);
  Location location = ps.token.location;

  std::vector<const Expr *> exprs;

  const Expr *string_expr_prefix = parse_string_expr_prefix(
      ps.token_and_advance());
  if (string_expr_prefix != nullptr) {
    exprs.push_back(string_expr_prefix);
  }

  while (ps.token.tk != tk_string_expr_suffix) {
    if (ps.token.tk == tk_string_expr_continuation) {
      throw user_error(ps.token.location, "found empty string expression");
    }

    const Expr *expr = parse_expr(ps, false /*allow_for_comprehensions*/);
    exprs.push_back(new Application(
        new Var(Identifier{"std.str", expr->get_location()}), expr));

    if (ps.token.tk == tk_string_expr_continuation) {
      const Expr *string_expr_continuation = parse_string_expr_continuation(
          ps.token_and_advance());
      if (string_expr_continuation != nullptr) {
        exprs.push_back(string_expr_continuation);
      }
    }
  }

  const Expr *string_expr_suffix = parse_string_expr_suffix(
      ps.token_and_advance());
  if (string_expr_suffix != nullptr) {
    exprs.push_back(string_expr_suffix);
  }

  if (exprs.size() == 1) {
    /* don't bother joining if it's just a single expr in a string */
    return exprs[0];
  } else {
    /* for now, just call join on all of these exprs, after ensuring that they
     * are stringified. */
    const Expr *str_array = build_array_literal(location, exprs);
    return new Application(
        new Var(ps.id_mapped(Identifier{"join", location})),
        {parse_string_literal(Token{location, tk_string, "\"\""}), str_array});
  }
}

const Expr *build_associative_array_literal(
    Location location,
    const std::vector<std::pair<const Expr *, const Expr *>> &exprs,
    bool is_set) {
  auto map_var = new Var(Identifier(fresh(), location));

  /* take all the exprs from the array, and turn them into statements to fill
   * out the structure */
  std::vector<const Expr *> stmts;
  types::Ref key_type;
  types::Ref value_type;

  for (auto expr : exprs) {
    if (value_type == nullptr) {
      value_type = type_variable(expr.second ? expr.second->get_location()
                                             : expr.first->get_location());
    }

    if (is_set) {
      assert(expr.second == nullptr);
      stmts.push_back(new Application(
          new Var(Identifier{"std.insert", expr.first->get_location()}),
          new Tuple(expr.first->get_location(),
                     {map_var,
                      new As(expr.first, value_type, false /*force_cast*/)})));
    } else {
      if (key_type == nullptr) {
        key_type = type_variable(expr.first->get_location());
      }
      stmts.push_back(new Application(
          new Var(
              Identifier{"std.set_indexed_item", expr.first->get_location()}),
          new Tuple(expr.first->get_location(),
                    {map_var,
                     new As(expr.first, key_type, false /*force_cast*/),
                     new As(expr.second, value_type, false /*force_cast*/)})));
    }
  }

  if (value_type == nullptr) {
    value_type = type_variable(location);
  }

  if (!is_set && key_type == nullptr) {
    key_type = type_variable(location);
  }

  /* now, add another item just for the actual array value to be returned */
  stmts.push_back(map_var);

  return new Let(
      map_var->id,
      new As(new Application(new Var(Identifier{"std.new", location}),
                             unit_expr(location)),
             is_set ? type_set_type(value_type)
                    : type_map_type(key_type, value_type),
             false /*force_cast*/),
      new Block(stmts));
}

const Expr *build_map_from_generator(Location location, const Expr *generator) {
  Identifier map_id{fresh(), location};

  /* take all the exprs from the array, and turn them into statements to fill
   * out the structure */
  types::Ref key_type = type_variable(generator->get_location());
  types::Ref value_type = type_variable(generator->get_location());

  return new Let(
      map_id,
      new As(new Application(new Var(Identifier{"std.new", location}),
                             unit_expr(location)),
             type_map_type(key_type, value_type), false /*force_cast*/),
      new Application(new Var(Identifier{"map.from_pairs", location}),
                      generator));
}

const Expr *parse_associative_array_literal(ParseState &ps) {
  auto start_curly_token = ps.token;
  chomp_token(tk_lcurly);
  /////
  std::vector<std::pair<const Expr *, const Expr *>> exprs;

  bool is_set = false;
  int i = 0;
  while (ps.token.tk != tk_rcurly && ps.token.tk != tk_none) {
    if (i != 0) {
      chomp_token(tk_comma);
      if (ps.token.tk == tk_rcurly) {
        break;
      }
    }
    ++i;

    const Expr *lhs = parse_expr(ps, false /*allow_for_comprehensions*/);
    auto prior_token = ps.token;
    if (ps.token.tk == tk_colon) {
      if (is_set) {
        throw user_error(ps.token.location,
                         "looks like you are mixing set literal syntax with "
                         "map literal syntax");
      }
      ps.advance();
      exprs.push_back(
          {lhs, parse_expr(ps, false /*allow_for_comprehensions*/)});
    } else if (ps.token.tk == tk_double_dot) {
      throw user_error(start_curly_token.location,
                       "range syntax is not allowed here");
    } else {
      if (i > 1 && !is_set) {
        throw user_error(ps.token.location,
                         "looks like you are mixing set literal syntax with "
                         "map literal syntax");
      }
      is_set = true;
      exprs.push_back({lhs, nullptr});
    }

    if (i == 1 && ps.token.is_ident(K(for))) {
      assert(exprs.size() == 1);
      /* this is a dictionary or set comprehension */
      if (is_set) {
        const Expr *ret = new Application(
            new Var(Identifier{"set.set", exprs[0].first->get_location()}),
            parse_generator(ps, exprs[0].first));
        chomp_token(tk_rcurly);
        return ret;
      } else {
        const Expr *ret = new Application(
            new Var(
                Identifier{"map.from_pairs", exprs[0].first->get_location()}),
            parse_generator(ps, new Tuple(prior_token.location,
                                          {exprs[0].first, exprs[0].second})));
        chomp_token(tk_rcurly);
        return ret;
      }
    }
  }

  chomp_token(tk_rcurly);

  return build_associative_array_literal(start_curly_token.location, exprs,
                                         is_set);
}

const Expr *parse_literal(ParseState &ps) {
  switch (ps.token.tk) {
  case tk_integer:
    return new Literal(ps.token_and_advance());
  case tk_char:
  case tk_float:
    return new Literal(ps.token_and_advance());
  case tk_string:
    if (ps.sugar_literals) {
      return parse_string_literal(ps.token_and_advance());
    } else {
      return new Literal(ps.token_and_advance());
    }
  case tk_string_expr_prefix:
    if (!ps.sugar_literals) {
      throw user_error(ps.token.location,
                       "sugared literals are not allowed here");
    } else {
      return parse_string_expr(ps);
    }
  case tk_lsquare:
    if (ps.sugar_literals) {
      return parse_array_literal(ps);
    } else {
      throw user_error(ps.token.location,
                       "array literals are not implemented in "
                       "this parse context");
    }
  case tk_lcurly:
    return parse_associative_array_literal(ps);

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

const Expr *parse_postfix_expr(ParseState &ps) {
  const Expr *expr = parse_base_expr(ps);

  while (!ps.line_broke() &&
         (ps.token.tk == tk_lsquare || ps.token.tk == tk_lparen ||
          ps.token.tk == tk_dot || ps.token.tk == tk_bang)) {
    switch (ps.token.tk) {
    case tk_bang:
      expr = new As(expr, type_unit(ps.token_and_advance().location),
                    true /*force_cast*/);
      break;
    case tk_lparen: {
      /* function call */
      auto location = ps.token.location;
      ps.advance();
      if (ps.token.tk == tk_rparen) {
        ps.advance();
        expr = new Application(expr, unit_expr(ps.token.location));
      } else {
        std::vector<const Expr *> callsite_refs;
        for (int index = 0; ps.token.tk != tk_rparen; ++index) {
          callsite_refs.push_back(
              parse_expr(ps, true /*allow_for_comprehensions*/));
          if (ps.token.tk == tk_comma) {
            ps.advance();
          } else {
            expect_token(tk_rparen);
          }
        }
        expr = new Application(expr, std::move(callsite_refs));
        chomp_token(tk_rparen);
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
      auto iid = Identifier{"__get_" + ps.token_and_advance().text,
                            ps.prior_token.location};
      expr = new Application(new Var(iid), expr);
      break;
    }
    case tk_lsquare: {
      ps.advance();
      bool is_slice = false;

      const Expr *start = nullptr;
      if (ps.token.tk == tk_colon) {
        start = new Literal(Token{ps.token.location, tk_integer, "0"});
      } else {
        start = parse_expr(ps, false /*allow_for_comprehensions*/);
      }

      if (ps.token.tk == tk_colon) {
        is_slice = true;
        ps.advance();
      }

      if (ps.token.tk == tk_rsquare) {
        ps.advance();
        if (ps.token.tk == tk_assign) {
          /* set up an array index assignment */
          auto location = ps.token_and_advance().location;
          auto rhs = parse_expr(ps, false /*allow_for_comprehensions*/);
          expr = new Application(
              new Var(Identifier{"std.set_indexed_item", location}),
              {expr, start, rhs});
        } else {
          expr = new Application(
              new Var(Identifier{is_slice ? "std.get_slice_from"
                                          : "std.get_indexed_item",
                                 ps.token.location}),
              {expr, start});
        }
      } else {
        const Expr *stop = parse_expr(ps, false /*allow_for_comprehensions*/);
        chomp_token(tk_rsquare);

        assert(is_slice);
        expr = new Application(
            new Var(ps.id_mapped(
                Identifier{"std.get_slice_from_to", ps.token.location})),
            {expr, start, stop});
      }
      break;
    }
    default:
      break;
    }
  }

  return expr;
}

const Expr *parse_cast_expr(ParseState &ps) {
  const Expr *expr = parse_postfix_expr(ps);
  while (!ps.line_broke() && ps.token.is_ident(K(as))) {
    ps.advance();
    bool force_cast = false;
    if (ps.token.tk == tk_bang && ps.token.follows_after(ps.prior_token)) {
      /* this is a force-cast */
      force_cast = true;
      ps.advance();
    }
    expr = new As(expr, parse_type(ps, true /*allow_top_level_application*/),
                  force_cast);
  }
  return expr;
}

const Expr *parse_sizeof(ParseState &ps) {
  auto location = ps.token.location;
  ps.advance();
  chomp_token(tk_lparen);
  auto type = parse_type(ps, true /*allow_top_level_application*/);
  chomp_token(tk_rparen);
  return new Sizeof(location, type);
}

const Expr *parse_prefix_expr(ParseState &ps) {
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

  const Expr *rhs;
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
      return new Application(
          new Var(ps.id_mapped(Identifier{"negate", prefix.t.location})),
          rhs);
    } else if (prefix.t.text == "!") {
      return new Application(
          new Var(Identifier{"std.load_value", prefix.t.location}), rhs);
    } else {
      return new Application(
          new Var(ps.id_mapped(Identifier{prefix.t.text, prefix.t.location})),
          rhs);
    }
  } else {
    return rhs;
  }
}

const Expr *parse_times_expr(ParseState &ps) {
  const Expr *expr = parse_prefix_expr(ps);

  while (!ps.line_broke() &&
         (ps.token.tk == tk_times || ps.token.tk == tk_divide_by ||
          ps.token.tk == tk_mod)) {
    Identifier op = ps.id_mapped(Identifier{ps.token.text, ps.token.location});
    ps.advance();

    expr = new Application(new Var(op), {expr, parse_prefix_expr(ps)});
  }

  return expr;
}

const Expr *parse_plus_expr(ParseState &ps) {
  auto expr = parse_times_expr(ps);

  while (!ps.line_broke() &&
         (ps.token.tk == tk_plus || ps.token.tk == tk_minus ||
          ps.token.tk == tk_backslash)) {
    Identifier op = ps.id_mapped(Identifier{ps.token.text, ps.token.location});
    ps.advance();

    expr = new Application(new Var(op), {expr, parse_times_expr(ps)});
  }

  return expr;
}

const Expr *parse_shift_expr(ParseState &ps) {
  auto expr = parse_plus_expr(ps);

  while (!ps.line_broke() &&
         (ps.token.tk == tk_shift_left || ps.token.tk == tk_shift_right)) {
    Identifier op = ps.id_mapped(Identifier{ps.token.text, ps.token.location});
    ps.advance();

    expr = new Application(new Var(op), {expr, parse_plus_expr(ps)});
  }

  return expr;
}

const Expr *parse_bitwise_and(ParseState &ps) {
  auto expr = parse_shift_expr(ps);

  while (!ps.line_broke() && ps.token.tk == tk_ampersand) {
    Identifier op = ps.id_mapped(Identifier{ps.token.text, ps.token.location});
    ps.advance();

    expr = new Application(new Var(op), {expr, parse_shift_expr(ps)});
  }

  return expr;
}

const Expr *parse_bitwise_xor(ParseState &ps) {
  auto expr = parse_bitwise_and(ps);

  while (!ps.line_broke() && ps.token.tk == tk_hat) {
    Identifier op = ps.id_mapped(Identifier{ps.token.text, ps.token.location});
    ps.advance();

    expr = new Application(new Var(op), {expr, parse_bitwise_and(ps)});
  }
  return expr;
}

const Expr *parse_bitwise_or(ParseState &ps) {
  auto expr = parse_bitwise_xor(ps);

  while (!ps.line_broke() && ps.token.tk == tk_pipe) {
    Identifier op = ps.id_mapped(Identifier{ps.token.text, ps.token.location});
    ps.advance();

    expr = new Application(new Var(op), {expr, parse_bitwise_xor(ps)});
  }

  return expr;
}

const Expr *fold_and_exprs(std::vector<const Expr *> exprs, int index) {
  if (index < int(exprs.size() - 1)) {
    Identifier term_id = make_iid(fresh());
    return new Let(term_id, exprs[index],
                   new Conditional(new Var(term_id),
                                   fold_and_exprs(exprs, index + 1),
                                   new Var(make_iid("std.False"))));
  } else {
    return exprs[index];
  }
}

const Expr *fold_or_exprs(std::vector<const Expr *> exprs, int index) {
  if (index < int(exprs.size() - 1)) {
    Identifier term_id = make_iid(fresh());
    return new Let(term_id, exprs[index],
                   new Conditional(new Var(term_id),
                                   new Var(make_iid("std.True")),
                                   fold_or_exprs(exprs, index + 1)));
  } else {
    return exprs[index];
  }
}

const Expr *foldl_application(
    ParseState &ps,
    const Expr *seed,
    std::list<const Expr *>::iterator terms_cur,
    const std::list<const Expr *>::iterator terms_end,
    std::list<Identifier>::iterator operands_cur,
    const std::list<Identifier>::iterator &operands_end) {
  const Expr *lhs = seed;
  if (terms_cur == terms_end) {
    return lhs;
  }
  const Expr *app = new Application(new Var(ps.id_mapped(*operands_cur++)),
                                    {lhs, *terms_cur++});
  return foldl_application(ps, app, terms_cur, terms_end, operands_cur,
                           operands_end);
}

const Expr *parse_comparison_expr(ParseState &ps) {
  /* all comparison operators have the same operator precedence and are left
   * associative */
  static const std::set<std::string> comparisons{
      "<", ">", "<=", ">=", "==", "!=", "in",
  };
  std::list<const Expr *> terms;
  std::list<Identifier> operands;
  do {
    terms.push_back(parse_bitwise_or(ps));
    bool not_ = false;
    if (ps.token.is_ident(K(not))) {
      ps.advance();
      not_ = true;
    }

    if (in(ps.token.text, comparisons)) {
      if (not_) {
        if (ps.token.text != "in") {
          throw user_error(ps.token.location,
                           "'not' is only valid before 'in'");
        } else {
          operands.push_back(Identifier{"not_in", ps.token.location});
          continue;
        }
      } // not-in

      operands.push_back(Identifier{ps.token.text, ps.token.location});
      continue;
    } else if (not_) {
      throw user_error(ps.token.location, "unexpected dangling 'not' operator");
    } else {
      break;
    }
  } while (ps.advance());

  if (terms.size() != operands.size() + 1) {
    throw user_error(ps.token.location,
                     "invalid number of terms to operands in prior comparison");
  }
  auto terms_iter = terms.begin();
  ++terms_iter;
  return foldl_application(ps, terms.front(), terms_iter, terms.end(),
                           operands.begin(), operands.end());
}

const Expr *parse_not_expr(ParseState &ps) {
  if (ps.token.is_ident(K(not))) {
    Token not_token = ps.token_and_advance();
    return new Application(new Var(ps.id_mapped(iid(not_token))),
                           parse_comparison_expr(ps));
  } else {
    return parse_comparison_expr(ps);
  }
}

const Expr *parse_and_expr(ParseState &ps) {
  std::vector<const Expr *> exprs;
  exprs.push_back(parse_not_expr(ps));

  while (!ps.line_broke() && (ps.token.is_ident(K(and)))) {
    ps.advance();
    exprs.push_back(parse_not_expr(ps));
  }

  return fold_and_exprs(exprs, 0);
}

const Expr *parse_tuple_expr(ParseState &ps) {
  auto start_token = ps.token;
  chomp_token(tk_lparen);
  if (ps.token.tk == tk_rparen) {
    /* we've got a reference to sole value of unit type */
    return unit_expr(ps.token_and_advance().location);
  }
  const Expr *expr = parse_expr(ps, true /*allow_for_comprehensions*/);
  if (ps.token.tk != tk_comma) {
    chomp_token(tk_rparen);
    return expr;
  } else {
    ps.advance();

    std::vector<const Expr *> exprs;

    exprs.push_back(expr);

    /* now let's find the rest of the values */
    while (true) {
      if (ps.token.tk == tk_rparen) {
        ps.advance();
        break;
      }
      exprs.push_back(parse_expr(ps, true /*allow_for_comprehensions*/));
      if (ps.token.tk == tk_comma) {
        ps.advance();
      }
      // continue and read the next parameter
    }

    return new Tuple(start_token.location, exprs);
  }
}

const Expr *parse_or_expr(ParseState &ps) {
  std::vector<const Expr *> exprs;
  exprs.push_back(parse_and_expr(ps));

  while (!ps.line_broke() && (ps.token.is_ident(K(or)))) {
    ps.advance();
    exprs.push_back(parse_and_expr(ps));
  }

  return fold_or_exprs(exprs, 0);
}

const Expr *parse_ternary_expr(ParseState &ps) {
  const Expr *condition = parse_or_expr(ps);
  if (ps.token.tk == tk_maybe) {
    ps.advance();

    const Expr *truthy_expr = parse_or_expr(ps);
    expect_token(tk_colon);
    ps.advance();
    return new Conditional(condition, truthy_expr, parse_or_expr(ps));
  } else {
    return condition;
  }
}

const Expr *parse_for_comprehension(ParseState &ps,
                                    bool allow_for_comprehensions) {
  const Expr *expr = parse_ternary_expr(ps);
  if (allow_for_comprehensions && ps.token.is_ident(K(for))) {
    return parse_generator(ps, expr);
  }
  return expr;
}

const Expr *parse_expr(ParseState &ps, bool allow_for_comprehensions) {
  return parse_for_comprehension(ps, allow_for_comprehensions);
}

const Expr *parse_assignment(ParseState &ps) {
  /* do not allow for comprehensions beause they would be at the statement
   * level which is better expressed as a regular for loop. */
  const Expr *lhs = parse_expr(ps, false /*allow_for_comprehensions*/);

  if (ps.line_broke()) {
    return lhs;
  }

  switch (ps.token.tk) {
  case tk_assign:
    ps.advance();
    return new Application(
        new Var(Identifier{"std.store_value", ps.token.location}),
        {lhs, parse_expr(ps, false /*allow_for_comprehensions*/)});
  case tk_divide_by_eq:
  case tk_minus_eq:
  case tk_mod_eq:
  case tk_plus_eq:
  case tk_times_eq: {
    auto op_token = ps.token_and_advance();
    assert(op_token.text.size() >= 1);
    const Expr *rhs = parse_expr(ps, false /*allow_for_comprehensions*/);
    Identifier copy_value = Identifier{fresh(), lhs->get_location()};
    return new Application(
        new Var(Identifier{"std.store_value", op_token.location}),
        {lhs, new Let(copy_value,
                      new Application(new Var(Identifier{"std.load_value",
                                                         op_token.location}),
                                      lhs),
                      new Application(
                          new Var(ps.id_mapped(Identifier{
                              op_token.text.substr(0, 1), op_token.location})),
                          {new Var(copy_value), rhs}))});
  }
  default:
    return lhs;
  }
}

const Expr *parse_block(ParseState &ps, bool expression_means_return) {
  BoundVarLifetimeTracker bvlt(ps);

  bool expression_block_syntax = false;
  Token expression_block_assign_token;
  bool finish_block = false;
  if (ps.token.tk == tk_lcurly) {
    finish_block = true;
    ps.advance();
    if (ps.token.tk == tk_rcurly) {
      if (expression_means_return) {
        return new ReturnStatement(unit_expr(ps.token_and_advance().location));
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
        if (auto expression = dcast<const Expr *>(statement)) {
          auto return_statement = new ReturnStatement(expression);
          statement = return_statement;
        }
      }

      if (ps.token.tk != tk_rparen && ps.token.tk != tk_rcurly &&
          ps.token.tk != tk_rsquare && ps.token.tk != tk_comma &&
          !ps.line_broke()) {
        throw user_error(ps.token.location,
                         "this looks hard to read. you should have a line "
                         "break after => blocks, unless they are immediately "
                         "followed by one of these: )]}");
      }
      return statement;
    } else {
      throw user_error(ps.token.location,
                       "empty expression blocks are not allowed");
    }
  } else {
    std::vector<const Expr *> stmts;
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
      return new Block(stmts);
    }
  }
}

const PatternBlock *parse_pattern_block(ParseState &ps,
                                        bool allow_else = true) {
  BoundVarLifetimeTracker bvlt(ps);

  return new PatternBlock(
      parse_predicate(ps, allow_else, maybe<Identifier>() /*name_assignment*/),
      parse_block(ps, false /*expression_means_return*/));
}

const Expr *parse_if(ParseState &ps) {
  if (ps.token.is_ident(K(if))) {
    ps.advance();
  } else {
    throw user_error(ps.token.location, "expected if");
  }

  Token condition_token = ps.token;
  const Expr *condition = parse_expr(ps, false /*allow_for_comprehensions*/);
  if (ps.token.is_ident(K(is))) {
    /* if ... is is a special form */
    Token is_token = ps.token;
    ps.advance();
    PatternBlocks pattern_blocks{parse_pattern_block(ps)};
    if (ps.token.is_ident(K(else))) {
      Token else_token = ps.token_and_advance();
      const Expr *else_block = ps.token.is_ident(K(if))
                                   ? parse_if(ps)
                                   : parse_block(
                                         ps, false /*expression_means_return*/);
      pattern_blocks.push_back(new PatternBlock(
          new IrrefutablePredicate(else_token.location, maybe<Identifier>()),
          else_block));
    } else {
      pattern_blocks.push_back(new PatternBlock(
          new IrrefutablePredicate(is_token.location, maybe<Identifier>()),
          unit_expr(is_token.location)));
    }
    return new Match(condition, pattern_blocks);
  } else {
    const Expr *block = parse_block(ps, false /*expression_means_return*/);
    const Expr *else_ = nullptr;
    /* check the successive instructions for "else if" or else */
    if (ps.token.is_ident(K(else))) {
      ps.advance();
      if (ps.token.is_ident(K(if))) {
        if (ps.line_broke()) {
          throw user_error(ps.token.location,
                           "else if must be on the same line");
        }
        else_ = parse_if(ps);
      } else {
        else_ = parse_block(ps, false /*expression_means_return*/);
      }
    }

    return new Conditional(condition, block,
                           else_ != nullptr ? else_
                                            : unit_expr(ps.token.location));
  }
}

const While *parse_while(ParseState &ps) {
  auto while_token = ps.token;
  chomp_ident(K(while));
  Token condition_token = ps.token;
  if (condition_token.is_ident(K(match))) {
    /* sugar for while match ... which becomes while true { match ... } */
    return new While(
        new Var(ps.id_mapped(Identifier{"True", while_token.location})),
        parse_match(ps));
  } else {
    const Expr *condition_expr = parse_expr(ps,
                                            false /*allow_for_comprehensions*/);
    if (ps.token.is_ident(K(is))) {
      auto is_token = ps.token;
      ps.advance();
      PatternBlocks pattern_blocks{
          parse_pattern_block(ps, false /*allow_else*/)};
      pattern_blocks.push_back(new PatternBlock(
          new IrrefutablePredicate(is_token.location, maybe<Identifier>()),
          new Break(is_token.location)));
      return new While(new Var(Identifier{"std.True", ps.token.location}),
                       new Match(condition_expr, pattern_blocks));
    } else {
      return new While(condition_expr,
                       parse_block(ps, false /*expression_means_return*/));
    }
  }
}

const Predicate *parse_ctor_predicate(ParseState &ps,
                                      maybe<Identifier> name_assignment) {
  assert(ps.token.tk == tk_identifier && isupper(ps.token.text[0]));
  Identifier ctor_name = ps.identifier_and_advance();

  std::vector<const Predicate *> params;
  if (ps.token.tk == tk_lparen) {
    ps.advance();
    while (ps.token.tk != tk_rparen) {
      params.push_back(parse_predicate(
          ps, false /*allow_else*/, maybe<Identifier>() /*name_assignment*/));
      if (ps.token.tk != tk_rparen) {
        chomp_token(tk_comma);
      }
    }
    chomp_token(tk_rparen);
  }
  return new CtorPredicate(ctor_name.location, params, ctor_name,
                           name_assignment);
}

const Predicate *parse_tuple_predicate(ParseState &ps,
                                       maybe<Identifier> name_assignment) {
  assert(ps.token.tk == tk_lparen);
  ps.advance();

  std::vector<const Predicate *> params;
  while (ps.token.tk != tk_rparen) {
    params.push_back(parse_predicate(ps, false /*allow_else*/,
                                     maybe<Identifier>() /*name_assignment*/));
    if (ps.token.tk != tk_rparen || params.size() == 1) {
      chomp_token(tk_comma);
    }
  }
  chomp_token(tk_rparen);

  return new TuplePredicate(ps.token.location, params, name_assignment);
}

const Predicate *parse_predicate(ParseState &ps,
                                 bool allow_else,
                                 maybe<Identifier> name_assignment,
                                 bool allow_var_refs) {
  if (ps.token.is_ident(K(else))) {
    if (!allow_else) {
      throw user_error(
          ps.token.location,
          "illegal keyword " c_type("%s") " in a pattern match context",
          ps.token.text.c_str());
    }
  } else if (!ps.token.is_ident(K(var)) &&
             is_restricted_var_name(ps.token.text)) {
    throw user_error(ps.token.location,
                     "irrefutable predicates are restricted to non-keyword "
                     "symbols (saw '%s')",
                     ps.token.text.c_str());
  }

  if (ps.token.tk == tk_lparen) {
    return parse_tuple_predicate(ps, name_assignment);
  } else if (ps.token.tk == tk_identifier) {
    /* match anything and give it a name */
    if (isupper(ps.token.text[0])) {
      /* match a ctor */
      return parse_ctor_predicate(ps, name_assignment);
    } else {
      if (name_assignment.valid) {
        throw user_error(
            ps.token.location,
            "pattern name assignment is only allowed once per term");
      }
      if (ps.token.is_ident(K(var))) {
        /* user is declaring this variable as mutable */
        ps.advance();
        expect_token(tk_identifier);
        /* check symbol validity again */
        if (isupper(ps.token.text[0])) {
          throw user_error(ps.token.location,
                           "constructor predicates cannot be 'var'");
        } else if (is_restricted_var_name(ps.token.text)) {
          throw user_error(ps.token.location,
                           "irrefutable predicates are restricted to "
                           "non-keyword symbols (saw '%s')",
                           ps.token.text.c_str());
        }
        /* this will be a var ref */
        ps.mutable_vars.insert(ps.token.text);
      } else {
        /* this will NOT be a var ref */
        ps.mutable_vars.erase(ps.token.text);
      }

      /* allow this variable to shadow anything in the term map */
      ps.term_map.erase(ps.token.text);

      /* match anything */
      Identifier symbol = iid(ps.token);
      ps.advance();
      if (ps.token.tk == tk_about) {
        ps.advance();

        return parse_predicate(ps, allow_else, maybe<Identifier>(symbol));
      } else {
        return new IrrefutablePredicate(symbol.location, symbol);
      }
    }
  } else {
    /* match a literal */
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
      return new Literal(ps.token_and_advance());
    }
    case tk_integer:
    case tk_float: {
      /* match a literal */
      const Predicate *literal = new Literal(
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

const Match *parse_match(ParseState &ps) {
  chomp_ident(K(match));
  bool auto_else = false;
  Token bang_token = ps.token;
  // TODO: this syntax needs some more thought. It's very subtle.
  if (ps.token.tk == tk_bang && ps.token.follows_after(ps.prior_token)) {
    auto_else = true;
    ps.advance();
  }
  auto scrutinee = parse_expr(ps, false /*allow_for_comprehensions*/);
  chomp_token(tk_lcurly);
  PatternBlocks pattern_blocks;
  while (ps.token.tk != tk_rcurly) {
    if (ps.token.is_ident(K(else))) {
      throw user_error(ps.token.location,
                       "place else patterns outside of the match block. (match "
                       "... { ... } else { ... })");
    }
    pattern_blocks.push_back(parse_pattern_block(ps, false /*allow_else*/));
  }
  chomp_token(tk_rcurly);
  if (auto_else) {
    auto pattern_block = new PatternBlock(
        new IrrefutablePredicate(bang_token.location, maybe<Identifier>()),
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
                     "match block did not have any patterns to match");
  }

  return new Match(scrutinee, pattern_blocks);
}

std::pair<Identifier, types::Ref> parse_lambda_param_core(ParseState &ps) {
  /* parse a parameter declaration for a lambda. */
  Token first_token = ps.token_and_advance();
  if (first_token.is_ident(K(var))) {
    expect_token(tk_identifier);
    Token param_token = ps.token_and_advance();

    /* add this param as a mutable var, since we know it's a Ref */
    ps.mutable_vars.insert(param_token.text);
    ps.term_map.erase(param_token.text);

    return {
        iid(param_token),
        type_operator(type_id(Identifier{"std.Ref", param_token.location}),
                      (token_begins_type(ps.token))
                          ? parse_type(ps, true /*allow_top_level_application*/)
                          : type_variable(param_token.location))};
  } else {
    /* remove this param from the outer term_map */
    if (isupper(first_token.text[0])) {
      throw user_error(first_token.location,
                       "parameter names cannot begin with capital letters");
    }
    ps.term_map.erase(first_token.text);
    return {iid(first_token),
            (token_begins_type(ps.token))
                ? parse_type(ps, true /*allow_top_level_application*/)
                : type_variable(first_token.location)};
  }
}

const Expr *parse_lambda(ParseState &ps,
                         TokenKind tk_start_param_list,
                         TokenKind tk_end_param_list) {
  if (ps.token.tk == tk_identifier) {
    throw user_error(ps.token.location, "identifiers are unexpected here");
  }

  BoundVarLifetimeTracker bvlt(ps);
  std::vector<Identifier> param_ids;
  types::Refs param_types;
  chomp_token(tk_start_param_list);

  while (!maybe_chomp_token(tk_end_param_list)) {
    if (param_ids.size() != 0 && ps.token.tk != tk_rparen) {
      /* chomp any delimiting commas */
      chomp_token(tk_comma);
    }

    auto id_type_pair = parse_lambda_param_core(ps);
    param_ids.push_back(id_type_pair.first);
    param_types.push_back(id_type_pair.second);
  }

  assert(param_ids.size() == param_types.size());
  if (param_ids.size() == 0) {
    param_types.push_back(type_unit(ps.token.location));
    param_ids.push_back(Identifier{"_", ps.token.location});
  }

  types::Ref return_type;
  if (token_begins_type(ps.token) && !ps.line_broke()) {
    return_type = parse_type(ps, true /*allow_top_level_application*/);
  }

  return new Lambda(param_ids, param_types, return_type,
                    parse_block(ps, true /*expression_means_return*/));
}

types::Ref parse_function_type(ParseState &ps) {
  chomp_token(tk_lparen);
  types::Refs params;
  while (true) {
    if (ps.token.tk == tk_rparen) {
      ps.advance();
      break;
    }
    params.push_back(parse_type(ps, true /*allow_top_level_application*/));
    if (ps.token.tk == tk_comma) {
      ps.advance();
    }
  }

  if (params.size() == 0) {
    params.push_back(type_unit(ps.prior_token.location));
  }

  if (token_begins_type(ps.token) && !ps.line_broke()) {
    params.push_back(parse_type(ps, true /*allow_top_level_application*/));
  } else {
    params.push_back(type_unit(ps.prior_token.location));
  }

  return type_arrows(params);
}

types::Ref parse_tuple_type(ParseState &ps) {
  chomp_token(tk_lparen);
  std::vector<types::Ref> dims;
  bool is_tuple = false;
  while (true) {
    if (ps.token.tk == tk_rparen) {
      if (dims.size() == 0) {
        is_tuple = true;
      }
      ps.advance();
      break;
    }

    dims.push_back(parse_type(ps, true /*allow_top_level_application*/));
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

types::Ref parse_square_type(ParseState &ps) {
  auto location = ps.token.location;
  chomp_token(tk_lsquare);
  auto lhs = parse_type(ps, true /*allow_top_level_application*/);
  if (ps.token.tk == tk_colon) {
    ps.advance();
    auto rhs = parse_type(ps, true /*allow_top_level_application*/);
    chomp_token(tk_rsquare);
    return type_map(lhs, rhs);
  } else {
    chomp_token(tk_rsquare);
    return type_operator(type_id(Identifier{VECTOR_TYPE, location}), lhs);
  }
}

types::Ref parse_named_type(ParseState &ps) {
  if (islower(ps.token.text[0])) {
    return type_variable(iid(ps.token_and_advance()));
  } else {
    return type_id(ps.identifier_and_advance());
  }
}

types::Ref parse_type(ParseState &ps, bool allow_top_level_application) {
  /* look for type application if allow_top_level_application */
  std::vector<types::Ref> types;
  if (ps.line_broke()) {
    throw user_error(
        ps.token.location,
        "encountered a line break where a type annotation was expected");
  }

  while (token_begins_type(ps.token) && !ps.line_broke() &&
         (allow_top_level_application || types.size() < 1)) {
    if (ps.token.tk == tk_lparen) {
      types.push_back(parse_tuple_type(ps));
    } else if (ps.token.tk == tk_lsquare) {
      types.push_back(parse_square_type(ps));
    } else if (ps.token.is_ident(K(fn))) {
      ps.advance();
      types.push_back(parse_function_type(ps));
    } else if (ps.token.is_ident(K(var))) {
      /* var syntax is a low-precedence unary type operator which applies the
       * "Ref" type to its operand */
      types::Ref ref_type_id = type_id(
          Identifier{"std.Ref", ps.token.location});
      ps.advance();

      types.push_back(type_operator(
          {ref_type_id, parse_type(ps, true /*allow_top_level_application*/)}));
    } else if (ps.token.tk == tk_identifier) {
      types.push_back(parse_named_type(ps));
    } else if (ps.token.tk == tk_times) {
      types.push_back(type_id(
          Identifier{PTR_TYPE_OPERATOR, ps.token_and_advance().location}));
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

const TypeDecl *parse_type_decl(ParseState &ps) {
  expect_token(tk_identifier);
  auto token = ps.token_and_advance();
  auto class_id = ps.id_mapped(iid(token));

  assert(ps.module_name.find(".") == std::string::npos);
  assert(token.text.find(".") == std::string::npos);

  if ((class_id.name != ps.module_name + "." + token.text) &&
      class_id.name.find(".") != std::string::npos) {
    throw user_error(class_id.location, "name %s is already defined as %s",
                     token.text.c_str(), class_id.str().c_str());
  }
  if (!isupper(token.text[0])) {
    throw user_error(
        class_id.location,
        "names in type-space must begin with an upper-case letter.")
        .add_info(class_id.location, "%s seems to break this rule",
                  token.text.c_str());
  }

  std::vector<Identifier> params;
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
  return new TypeDecl{class_id, params};
}

types::Ref create_ctor_type(Location location,
                            const TypeDecl *type_decl,
                            types::Refs param_types) {
  /* push the return type on as the final type */
  param_types.push_back(type_decl->get_type());
  auto type = type_arrows(param_types);

  for (int i = type_decl->params.size() - 1; i >= 0; --i) {
    type = type_lambda(type_decl->params[i], type);
  }
  return type;
}

const Expr *create_ctor(Location location,
                        int ctor_id,
                        const TypeDecl *type_decl,
                        types::Refs param_types) {
  std::vector<const Expr *> dims;
  /* add the ctor's id value as the first element in the tuple */
  dims.push_back(
      new Literal({location, tk_integer, string_format("%d", ctor_id)}));

  std::vector<Identifier> params;
  for (size_t i = 0; i < param_types.size(); ++i) {
    /* enumerate the nested lambda variables */
    params.push_back(Identifier{fresh(), param_types[i]->get_location()});
    dims.push_back(new Var(params.back()));
  }

  const Expr *expr = new As(new Tuple(location, dims), type_decl->get_type(),
                            true /*force_cast*/);

  if (params.size() > 0) {
    /* this ctor takes parameters, so it needs a lambda */
    assert(dims.size() == params.size() + 1);
    /* (λx y z . return! (ctor_id, x, y, z) as! type_decl) */
    expr = new Lambda(params, param_types, nullptr, new ReturnStatement(expr));
  }

  return expr;
}

struct DataTypeDecl {
  const TypeDecl *type_decl;
  std::vector<const Decl *> decls;
};

DataTypeDecl parse_struct_decl(ParseState &ps, types::Map &data_ctors) {
  const TypeDecl *type_decl = parse_type_decl(ps);
  std::vector<const Decl *> decls;
  types::Refs dims;
  Identifiers member_ids;

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
    dims.push_back(parse_type(ps, true /*allow_top_level_application*/));
  }

  /* create accessor functions */
  for (size_t i = 1 /*skip the ctor_id*/; i < dims.size(); ++i) {
    decls.push_back(new Decl(
        /* accessor function names look like __.x */
        make_accessor_id(member_ids[i]),
        new Lambda(
            {Identifier{"obj", member_ids[i].location}},
            {type_decl->get_type()}, dims[i],
            new ReturnStatement(new TupleDeref(
                new As(new Var(Identifier{"obj", member_ids[i].location}),
                       type_tuple(dims), true /*force_cast*/),
                i, dims.size())))));
  }

  chomp_token(tk_rcurly);

  /* we don't need the ctor_id below */
  dims = vec_slice(dims, 1, dims.size());

  /* there is only one ctor for structs which are just product types */
  auto ctor_id = type_decl->id;

  data_ctors[ctor_id.name] = create_ctor_type(ctor_id.location, type_decl,
                                              dims);
  decls.push_back(
      new Decl(ctor_id, create_ctor(ctor_id.location,
                                    // TODO: replace this construct with a
                                    // newtype tuple once newtype exists.
                                    0 /*ctor_id*/, type_decl, dims)));
  // log("parsed struct with decls %s", join_str(decls, ", ").c_str());

  return {type_decl, decls};
}

DataTypeDecl parse_newtype_decl(ParseState &ps,
                                types::Map &data_ctors,
                                ParsedCtorIdMap &ctor_id_map) {
  expect_token(tk_identifier);
  Token type_name = ps.token;
  const TypeDecl *type_decl = parse_type_decl(ps);

  chomp_token(tk_assign);
  if (ps.token.tk != tk_identifier || ps.token.text != type_name.text) {
    throw user_error(ps.token.location,
                     "newtype must have a single constructor whose name "
                     "matches the type name");
  }

  chomp_token(tk_identifier);
  types::Ref rhs_type = parse_type(ps, true /*allow_top_level_application*/);

  const Decl *decl;
  std::vector<types::Ref> ctor_parts;
  if (auto tuple_type = dyncast<const types::TypeTuple>(rhs_type)) {
    if (tuple_type->dimensions.size() == 1) {
      std::cerr << "neutering unary tuple" << std::endl;
      /* simply disallow unary tuples in newtypes */
      rhs_type = tuple_type->dimensions.at(0);
    }
  }

  // This is so hacky.
#if 0
  if (auto tuple_type = dyncast<const types::TypeTuple>(rhs_type)) {
    debug_above(2, log("build decl for tuple newtype ctor :: " c_id("%s") "%s",
                       type_name.text.c_str(), tuple_type->str().c_str()));
    ctor_parts = tuple_type->dimensions;
    std::vector<Identifier> dim_names;
    std::vector<const Expr *> dims;
    for (size_t i = 0; i < tuple_type->dimensions.size(); ++i) {
      dim_names.push_back(
          Identifier{ast::fresh(), tuple_type->dimensions[i]->get_location()});
      dims.push_back(new Var(dim_names.back()));
    }

    decl = new Decl(
        type_decl->id,
        new Lambda(dim_names, tuple_type->dimensions,
                   type_variable(INTERNAL_LOC()),
                   new ReturnStatement(
                       new As(new Tuple(tuple_type->get_location(), dims),
                              type_decl->get_type(), true /*force_cast*/))));
  } else 
#endif
  {
    ctor_parts.push_back(rhs_type);
    Identifier param_iid = Identifier{ast::fresh(), rhs_type->get_location()};
    decl = new Decl(type_decl->id,
                    new Lambda({param_iid}, {rhs_type}, type_decl->get_type(),
                               new ReturnStatement(new As(
                                   new Var(param_iid), type_decl->get_type(),
                                   true /*force_cast*/))));
  }
  /* add this newtype's data ctor to the registered set */
  data_ctors[type_decl->id.name] = create_ctor_type(type_decl->id.location,
                                                    type_decl, ctor_parts);
  assert(!in(type_decl->id.name, ps.type_env));
  /* because this is a newtype, we need to remember the type mapping within
   * the type environment for reference later in pattern matching, and in code
   * generation. */
  types::Ref body = rhs_type;
  for (auto param_iter = type_decl->params.rbegin();
       param_iter != type_decl->params.rend(); ++param_iter) {
    auto param = *param_iter;
    body = type_lambda(param, body);
  }
  debug_above(2, log_location(type_decl->id.location,
                              "adding " c_id("%s") " to the type_env as %s",
                              type_decl->id.name.c_str(), body->str().c_str()));
  ps.type_env[type_decl->id.name] = body;
  return {type_decl, {decl}};
}

DataTypeDecl parse_data_type_decl(ParseState &ps,
                                  types::Map &data_ctors,
                                  ParsedCtorIdMap &ctor_id_map) {
  const TypeDecl *type_decl = parse_type_decl(ps);

  chomp_token(tk_lcurly);
  struct DataCtorParts {
    Token ctor_token;
    types::Refs param_types;
  };
  std::list<std::unique_ptr<DataCtorParts>> data_ctors_parts;

  size_t param_types_count = 0;
  while (true) {
    expect_token(tk_identifier);

    std::unique_ptr<DataCtorParts> data_ctor_parts =
        std::make_unique<DataCtorParts>();
    data_ctor_parts->ctor_token = ps.token_and_advance();
    if (ps.token.tk == tk_lparen) {
      ps.advance();
      /* this is a data ctor */
      while (true) {
        /* parse the types of the dimensions (unnamed for now) */
        data_ctor_parts->param_types.push_back(
            parse_type(ps, true /*allow_top_level_application*/));
        /* keep track of whether any of the values in this data type require
         * extra storage. NB: Bool only being 1 word in size relies on this.
         */
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

  std::vector<const Decl *> decls;
  if (param_types_count == 0) {
    /* this is just an ENUM. this type can be simplified to just an Int */
    ps.type_env[type_decl->id.name] = type_id(make_iid(INT_TYPE));

    /* build the decls for the various values */
    int i = 0;
    for (auto &data_ctor_parts : data_ctors_parts) {
      auto ctor_id = iid(data_ctor_parts->ctor_token);
      debug_above(3, log_location(ctor_id.location, "creating enum type for %s",
                                  ctor_id.str().c_str()));
      data_ctors[ctor_id.name] = type_decl->get_type();
      decls.push_back(new Decl(
          ctor_id, new As(new Literal(Token{ctor_id.location, tk_integer,
                                            std::to_string(i)}),
                          type_decl->get_type(), true /*force_cast*/)));
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
          new Decl(ctor_id, create_ctor(ctor_id.location, i, type_decl,
                                        data_ctor_parts->param_types)));
      ctor_id_map[ctor_id.name] = i++;
    }
  }

  return {type_decl, decls};
}

types::ClassPredicateRef parse_class_predicate(ParseState &ps) {
  expect_token(tk_identifier);
  if (!isupper(ps.token.text[0])) {
    throw user_error(ps.token.location,
                     "type class requirements need to be upper-case "
                     "because type classes need to be uppercase");
  }
  Identifier classname = ps.id_mapped(
      Identifier::from_token(ps.token_and_advance()));
  types::Refs type_parameters;

  while (!ps.line_broke() && ps.token.tk != tk_lcurly) {
    type_parameters.push_back(
        parse_type(ps, false /*allow_top_level_application*/));
  }
  return std::make_shared<types::ClassPredicate>(classname, type_parameters);
}

std::vector<const Decl *> parse_decls(ParseState &ps) {
  chomp_token(tk_lcurly);

  std::vector<const Decl *> decls;
  while (true) {
    if (ps.token.is_ident(K(fn))) {
      /* instance-level functions */
      ps.advance();
      auto token = ps.token_and_advance();
      auto id = ps.id_mapped(Identifier{token.text, token.location});
      decls.push_back(new Decl(id, parse_lambda(ps)));
    } else if (ps.token.tk != tk_rcurly) {
      /* instance-level let vars */
      auto name_token = ps.token_and_advance();
      auto id = ps.id_mapped(Identifier{name_token.text, name_token.location});
      chomp_token(tk_assign);
      decls.push_back(
          new Decl(id, parse_expr(ps, false /*allow_for_comprehensions*/)));
    } else {
      chomp_token(tk_rcurly);
      break;
    }
  }
  return decls;
}

const Instance *parse_type_class_instance(ParseState &ps) {
  types::ClassPredicateRef class_predicate = parse_class_predicate(ps);
  return new Instance{class_predicate, parse_decls(ps)};
}

const TypeClass *parse_type_class(ParseState &ps) {
  const TypeDecl *type_decl = parse_type_decl(ps);

  if (type_decl->params.size() == 0) {
    throw user_error(
        type_decl->id.location,
        "type classes must be parameterized over at least one type variable");
  }

  /* Check for duplicate type class params */
  {
    std::unordered_set<std::string> params;
    for (auto &param : type_decl->params) {
      if (in(param.name, params)) {
        throw user_error(param.location,
                         "type class parameter " c_type("%s") " is repeated",
                         param.name.c_str());
      }
      params.insert(param.name);
    }
  }

  chomp_token(tk_lcurly);
  types::ClassPredicates class_predicates;
  types::Map overloads;
  std::vector<const Decl *> default_decls;

  while (true) {
    if (ps.token.is_ident(K(has))) {
      ps.advance();
      class_predicates.insert(parse_class_predicate(ps));
    } else if (ps.token.is_ident(K(fn))) {
      /* an overloaded function */
      ps.advance();
      auto id = Identifier{ps.token.text, ps.token.location};
      ps.advance();
      overloads[id.name] = parse_function_type(ps);
    } else {
      break;
    }
  }

  if (ps.token.is_ident(K(default))) {
    ps.advance();
    default_decls = parse_decls(ps);
  }
  chomp_token(tk_rcurly);
  return new TypeClass(type_decl->id, type_decl->params, class_predicates,
                       overloads, default_decls);
}

const Module *parse_module(ParseState &ps,
                           std::vector<const Module *> auto_import_modules,
                           std::set<Identifier> &module_deps) {
  debug_above(6, log("about to parse %s", ps.filename.c_str()));

  for (auto aim : auto_import_modules) {
    if (aim == nullptr) {
      continue;
    }
    std::set<std::string> tlds = compiler::get_top_level_decls(
        aim->decls, aim->type_decls, aim->type_classes, aim->imports);
    for (auto tld : tlds) {
      debug_above(9, log("adding tld %s -> %s in %s", tld.c_str(),
                         (aim->name + "." + tld).c_str(), ps.filename.c_str()));
      /* imports cannot override other imports */
      ps.add_term_map(INTERNAL_LOC(), tld, aim->name + "." + tld,
                      false /*allow_override*/);
    }
  }

  std::vector<const Decl *> decls;
  std::vector<const TypeDecl *> type_decls;
  std::vector<const TypeClass *> type_classes;
  std::vector<const Instance *> instances;

  while (ps.token.is_ident(K(import))) {
    ps.advance();
    expect_token(tk_identifier);
    Identifier module_name = iid(ps.token);
    ps.advance();

    if (ps.token.tk == tk_lcurly) {
      ps.advance();
      while (true) {
        if (ps.token.tk == tk_comma) {
          throw user_error(ps.token.location, "unexepected comma");
        }
        Identifier symbol = Identifier{ps.token.text, ps.token.location};
        ps.advance();
        if (starts_with(symbol.name, "_")) {
          throw user_error(symbol.location,
                           "it is not possible to import module-scoped "
                           "variables into other modules");
        }
        /* record this import */
        debug_above(3, log_location(ps.token.location, "recording import of %s",
                                    symbol.str().c_str()));
        ps.symbol_imports[ps.module_name][module_name.name].insert(symbol);
        assert(symbol.name.find(".") == std::string::npos);
        ps.add_term_map(symbol.location, symbol.name,
                        module_name.name + "." + symbol.name,
                        false /*allow_override*/);
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

  /* for now we will only allow exports at the top. trying to be draconian
   * with module layout */
  std::set<Identifier> exports;
  while (ps.token.is_ident(K(export))) {
    ps.advance();
    chomp_token(tk_lcurly);

    while (ps.token.tk != tk_rcurly) {
      if (ps.token.tk == tk_comma) {
        throw user_error(ps.token.location, "unexepected comma");
      }
      Identifier symbol = Identifier{ps.token.text, ps.token.location};
      ps.advance();
      if (in(symbol, exports)) {
        auto iter = exports.find(symbol);
        assert(iter != exports.end());
        throw user_error(symbol.location, "duplicate symbol %s in exports",
                         symbol.str().c_str())
            .add_info(iter->location, "see previous export");
      }
      assert(symbol.name.find(".") == std::string::npos);
      exports.insert(symbol);
      if (ps.token.tk != tk_rcurly) {
        chomp_token(tk_comma);
      }
    }
    chomp_token(tk_rcurly);
  }

  for (auto &id : exports) {
    assert(id.name.find(".") == std::string::npos);
    auto fully_qualified_id = Identifier{ps.module_name + "." + id.name,
                                         id.location};
    auto imported_id = ps.id_mapped(id);
    ps.symbol_exports[ps.module_name][fully_qualified_id] =
        (imported_id.name.find(".") != std::string::npos &&
         !starts_with(imported_id.name, GLOBAL_SCOPE_NAME "."))
            ? imported_id
            : fully_qualified_id;
    /* in order to allow definitions in auto_imported modules, remap the
     * exported symbol to the local namespace for parsing purposes */
    ps.add_term_map(id.location, id.name, fully_qualified_id.name,
                    true /*allow_override*/);
  }

  while (ps.token.is_ident(K(link))) {
    ps.advance();
    if (ps.token.is_ident(K(pkg))) {
      ps.advance();
      ps.link_ins.insert(LinkIn{lit_pkgconfig, ps.token_and_advance()});
    } else if (ps.token.is_ident(K(in))) {
      ps.advance();
      ps.link_ins.insert(LinkIn{lit_compile, ps.token_and_advance()});
    } else if (ps.token.tk == tk_string) {
      ps.link_ins.insert(LinkIn{lit_link, ps.token_and_advance()});
    } else {
      throw user_error(ps.token.location, "unexpected link directive");
    }
  }

  while (true) {
    if (ps.token.is_ident(K(import))) {
      throw user_error(ps.token.location,
                       "import statements must occur at the top of the module");
    } else if (ps.token.is_ident(K(export))) {
      throw user_error(ps.token.location,
                       "export statements must occur at the top of the module, "
                       "or just below any existing imports");
    } else if (ps.token.is_ident(K(fn))) {
      /* module-level functions */
      ps.advance();
      auto id = Identifier::from_token(ps.token_and_advance());
      decls.push_back(new Decl(id, parse_lambda(ps)));
    } else if (ps.token.is_ident(K(struct))) {
      ps.advance();
      types::Map data_ctors;
      auto data_type = parse_struct_decl(ps, data_ctors);
      type_decls.push_back(data_type.type_decl);
      for (auto &decl : data_type.decls) {
        decls.push_back(decl);
      }
      ps.data_ctors_map[data_type.type_decl->id.name] = data_ctors;
    } else if (ps.token.is_ident(K(newtype))) {
      /* module-level newtypes */
      ps.advance();
      types::Map data_ctors;
      DataTypeDecl data_type = parse_newtype_decl(ps, data_ctors,
                                                  ps.ctor_id_map);
      type_decls.push_back(data_type.type_decl);
      for (auto &decl : data_type.decls) {
        decls.push_back(decl);
      }
      ps.data_ctors_map[data_type.type_decl->id.name] = data_ctors;
    } else if (ps.token.is_ident(K(data))) {
      /* module-level data types */
      ps.advance();
      types::Map data_ctors;
      DataTypeDecl data_type = parse_data_type_decl(ps, data_ctors,
                                                    ps.ctor_id_map);
      type_decls.push_back(data_type.type_decl);
      for (auto &decl : data_type.decls) {
        decls.push_back(decl);
      }
      ps.data_ctors_map[data_type.type_decl->id.name] = data_ctors;
    } else if (ps.token.is_ident(K(let))) {
      /* module-level constants */
      ps.advance();
      auto id = Identifier::from_token(ps.token_and_advance());
      chomp_token(tk_assign);
      decls.push_back(
          new Decl(id, parse_expr(ps, false /*allow_for_comprehensions*/)));
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

  std::vector<Identifier> imports;
  std::set<Identifier> imports_set;

  for (auto &dest_pair : get(ps.symbol_imports, ps.module_name,
                             std::map<std::string, std::set<Identifier>>{})) {
#ifdef ZION_DEBUG
    const std::string &dest_module = dest_pair.first;
#endif
    const std::set<Identifier> &symbols = dest_pair.second;
    for (auto &symbol : symbols) {
      debug_above(2, log("adding import from {%s: {..., %s: %s, ...}",
                         ps.module_name.c_str(), dest_module.c_str(),
                         symbol.str().c_str()));
      assert(imports_set.count(symbol) == 0);
      imports_set.insert(symbol);
      imports.push_back(symbol);
    }
  }

  return new Module(ps.module_name, imports, decls, type_decls, type_classes,
                    instances, ps.ctor_id_map, ps.data_ctors_map, ps.type_env);
}

} // namespace parser
} // namespace zion
