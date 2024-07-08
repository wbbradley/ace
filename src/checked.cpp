#include "checked.h"

namespace cider {

CheckedDefinition::CheckedDefinition(types::SchemeRef scheme,
                                     const ast::Decl *decl,
                                     TrackedTypes tracked_types)
    : scheme(scheme), decl(decl), tracked_types(tracked_types) {
  debug_above(3, log("creating CheckedDefinition of %s with scheme %s",
                     decl->str().c_str(), scheme->str().c_str()));
}

Location CheckedDefinition::get_location() const {
  return decl->get_location();
}

} // namespace cider
