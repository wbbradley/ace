#pragma once

#include "ast.h"
#include "class_predicate.h"
#include "constraint.h"
#include "env.h"
#include "scheme_resolver.h"

namespace zion {
types::Ref infer(const bitter::Expr *expr,
                 Env &env,
                 const types::SchemeResolver &scheme_resolver,
                 types::Constraints &constraints,
                 types::ClassPredicates &instance_requirements);
}
