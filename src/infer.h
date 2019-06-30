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

typedef std::vector<Constraint> Constraints;
types::Ref infer(bitter::Expr *expr, Env &env, Constraints &constraints);
std::string str(const Constraints &constraints);
