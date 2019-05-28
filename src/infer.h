#pragma once
#include <tuple>
#include <vector>

#include "context.h"
#include "env.h"
#include "types.h"

struct constraint_t {
  constraint_t() = delete;
  constraint_t(types::type_t::ref a, types::type_t::ref b, context_t &&context);

  types::type_t::ref a;
  types::type_t::ref b;
  context_t context;

  void rebind(const types::type_t::map &env);
  std::string str() const;
};
typedef std::vector<constraint_t> constraints_t;
types::type_t::ref infer(bitter::expr_t *expr,
                         env_t &env,
                         constraints_t &constraints);
std::string str(const constraints_t &constraints);
