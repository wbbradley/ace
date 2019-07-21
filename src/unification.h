#pragma once
#include <memory>
#include <string>
#include <vector>

#include "zion_assert.h"
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
types::SchemeRef scheme_unify(types::Scheme::Ref a, types::Scheme::Ref b);
bool scheme_equality(types::Scheme::Ref a, types::Scheme::Ref b);

} // namespace types

#define assert_type_equality(a, b)                                             \
  do {                                                                         \
    auto __a = (a);                                                            \
    auto __b = (b);                                                            \
    if (!type_equality(__a, __b)) {                                            \
      log_location(log_error,                                                  \
                   best_location(__a->get_location(), __b->get_location()),    \
                   "types %s and %s are not equal!", __a->str().c_str(),       \
                   __b->str().c_str());                                        \
      assert(false);                                                           \
    }                                                                          \
  } while (0)
