#pragma once
#include <tuple>
#include <vector>

#include "context.h"
#include "env.h"
#include "types.h"

struct Constraint {
  Constraint() = delete;
  Constraint(types::Ref a, types::Ref b, Context &&context);

  types::Ref a;
  types::Ref b;
  Context context;

  void rebind(const types::Map &env);
  std::string str() const;
};
typedef std::vector<Constraint> constraints_t;
types::Ref infer(bitter::Expr *expr, Env &env, constraints_t &constraints);
std::string str(const constraints_t &constraints);
