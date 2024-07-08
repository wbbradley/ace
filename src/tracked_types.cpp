#include "tracked_types.h"

#include "ast.h"
#include "dbg.h"
#include "logger_decls.h"

namespace ace {

types::Ref get_tracked_type(const TrackedTypes &tracked_types,
                            const ast::Expr *e) {
  auto iter = tracked_types.find(e);
  if (iter == tracked_types.end()) {
    log_location(log_error, e->get_location(),
                 "translation env does not contain a type for %s",
                 e->str().c_str());
    assert(false && !!"missing type for expression");
    return {};
  } else {
    return iter->second;
  }
}

void rebind_tracked_types(TrackedTypes &tracked_types,
                          const types::Map &bindings) {
  if (bindings.size() == 0) {
    return;
  }

  for (auto pair : tracked_types) {
    if (pair.second->ftv_count() != 0) {
      debug_above(7, log("changing tracked_types[%s] from %s to %s",
                         pair.first->str().c_str(), pair.second->str().c_str(),
                         pair.second->rebind(bindings)->str().c_str()));
      tracked_types[pair.first] = pair.second->rebind(bindings);
    }
  }
}

} // namespace ace
