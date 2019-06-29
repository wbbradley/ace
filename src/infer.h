#pragma once
#include <tuple>
#include <vector>

#include "context.h"
#include "env.h"
#include "types.h"

struct Constraint {
  Constraint() = delete;
  Constraint(types::Type::ref a, types::Type::ref b, Context &&context);

  types::Type::ref a;
  types::Type::ref b;
  Context context;

  void rebind(const types::Type::map &env);
  std::string str() const;
};
typedef std::vector<Constraint> constraints_t;
types::Type::ref infer(bitter::Expr *expr,
                       Env &env,
                       constraints_t &constraints);
std::string str(const constraints_t &constraints);
