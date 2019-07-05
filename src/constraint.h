#pragma once

#include <string>
#include <vector>

#include "context.h"
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
std::string str(const Constraints &constraints);

void append_to_constraints(Constraints &constraints,
                           types::Ref a,
                           types::Ref b,
                           Context &&context);
