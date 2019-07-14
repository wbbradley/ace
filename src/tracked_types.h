#pragma once

#include <unordered_map>

#include "ast_decls.h"
#include "types.h"

namespace zion {

typedef std::unordered_map<const zion::ast::Expr *, types::Ref> TrackedTypes;

types::Ref get_tracked_type(const TrackedTypes &tracked_types,
                            const ast::Expr *e);
void rebind_tracked_types(TrackedTypes &tracked_types,
                          const types::Map &bindings);

} // namespace zion
