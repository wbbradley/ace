#pragma once

#include "constraint.h"
#include "context.h"
#include "env.h"
#include "scheme_resolver.h"
#include "types.h"

namespace zion {

types::Map solver(bool check_constraint_coverage,
                  Context &&context,
                  types::Constraints &constraints,
                  Env &env,
                  const types::SchemeResolver &scheme_resolver,
                  types::ClassPredicates &instance_requirements);

} // namespace zion
