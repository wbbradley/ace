#pragma once

#include "ast.h"
#include "class_predicate.h"
#include "constraint.h"
#include "data_ctors_map.h"
#include "scheme_resolver.h"
#include "tracked_types.h"

namespace ace {
types::Ref infer(const ast::Expr *expr,
                 const DataCtorsMap &data_ctors_map,
                 const types::Ref &return_type,
                 const types::SchemeResolver &scheme_resolver,
                 TrackedTypes &tracked_types,
                 types::Constraints &constraints,
                 types::ClassPredicates &instance_requirements);
} // namespace ace
