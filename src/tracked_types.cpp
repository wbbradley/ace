#include "tracked_types.h"

#include "ast.h"
#include "logger_decls.h"

namespace zion {

types::Ref get_tracked_type(const TrackedTypes &tracked_types,
                            const ast::Expr *e) {
  auto iter = tracked_types.find(e);
  if (iter == tracked_types.end()) {
    log_location(log_error, e->get_location(),
                 "translation env does not contain a type for %s",
                 e->str().c_str());
    assert(false && !!"missing type for expression");
  } else {
    return iter->second;
  }
}

void rebind_tracked_types(TrackedTypes &tracked_types,
                          const types::Map &bindings) {
  if (bindings.size() == 0) {
    return;
  }

  TrackedTypes temp_tracked_types;

  for (auto pair : tracked_types) {
    assert(temp_tracked_types.count(pair.first) == 0);
    temp_tracked_types.insert({pair.first, pair.second->rebind(bindings)});
  }
  temp_tracked_types.swap(tracked_types);
}

} // namespace zion
