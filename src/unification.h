#pragma once
#include <memory>
#include <string>
#include <vector>

#include "location.h"
#include "types.h"

namespace types {

/* Unification is basically just an Either result type. */
struct Unification {
  bool result;
  Location error_location;
  std::string error_string;
  types::Map bindings;
};

Unification unify(types::Ref a, types::Ref b);
Unification unify_many(const types::Refs &as, const types::Refs &b);
types::Map compose(const types::Map &a, const types::Map &b);
Unification compose(const Unification &a, const Unification &b);
bool type_equality(types::Ref a, types::Ref b);
bool scheme_equality(types::Scheme::Ref a, types::Scheme::Ref b);

} // namespace types
