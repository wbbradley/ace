#pragma once

#include <list>
#include <map>
#include <memory>
#include <string>

namespace zion {
struct CheckedDefinition;
typedef std::shared_ptr<const CheckedDefinition> CheckedDefinitionRef;
typedef std::map<std::string, std::list<CheckedDefinitionRef>>
    CheckedDefinitionsByName;
} // namespace zion

#include "ast.h"
#include "scheme.h"
#include "types.h"

namespace zion {

struct CheckedDefinition {
  CheckedDefinition(types::SchemeRef scheme,
                    const ast::Decl *decl,
                    TrackedTypes tracked_types);
  types::SchemeRef scheme;
  const ast::Decl *decl;
  TrackedTypes tracked_types;

  Location get_location() const;
};

} // namespace zion
