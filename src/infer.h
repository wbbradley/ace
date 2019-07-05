#pragma once

#include "ast.h"
#include "class_predicate.h"
#include "constraint.h"
#include "env.h"

types::Ref infer(const bitter::Expr *expr,
                 Env &env,
                 Constraints &constraints,
                 types::ClassPredicates &instance_requirements);
