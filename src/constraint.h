#pragma once

#include <string>
#include <vector>

#include "context.h"
#include "types.h"

namespace types {

struct Constraint {
  Constraint() = delete;
  Constraint(types::Ref a, types::Ref b, Context &&context);

  Ref a, b;
  Context context;

  void rebind(const Map &env);

  std::string str() const;
};

typedef std::vector<Constraint> Constraints;
std::string str(const Constraints &constraints);

void append_to_constraints(Constraints &constraints,
                           Ref a,
                           Ref b,
                           Context &&context);
void rebind_constraints(Constraints::iterator iter,
                        const Constraints::iterator &end,
                        const Map &bindings);

} // namespace types
