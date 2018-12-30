#pragma once
#include "ast.h"
#include "env.h"
#include "types.h"

bitter::expr_t *translate(bitter::expr_t *expr, types::type_t::ref type, const env_t &env);
