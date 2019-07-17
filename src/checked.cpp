#include "checked.h"

namespace zion {

CheckedDefinition::CheckedDefinition(types::SchemeRef scheme,
                                     const ast::Decl *decl,
                                     TrackedTypes tracked_types)
    : scheme(scheme), decl(decl), tracked_types(tracked_types) {
}

Location CheckedDefinition::get_location() const {
  return decl->get_location();
}

} // namespace zion
