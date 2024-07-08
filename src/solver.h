#pragma once

#include "constraint.h"
#include "context.h"
#include "scheme_resolver.h"
#include "tracked_types.h"
#include "types.h"

namespace cider {

types::Map solver(bool check_constraint_coverage,
                  Context &&context,
                  types::Constraints &constraints,
                  TrackedTypes &tracked_types,
                  const types::SchemeResolver &scheme_resolver,
                  types::ClassPredicates &instance_requirements);

} // namespace cider
