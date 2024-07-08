#pragma once

#include <list>
#include <map>
#include <memory>
#include <string>

namespace cider {
struct CheckedDefinition;
typedef std::shared_ptr<const CheckedDefinition> CheckedDefinitionRef;
typedef std::map<std::string, std::list<CheckedDefinitionRef>>
    CheckedDefinitionsByName;
} // namespace cider

#include "ast.h"
#include "scheme.h"
#include "types.h"

namespace cider {

struct CheckedDefinition {
  CheckedDefinition(types::SchemeRef scheme,
                    const ast::Decl *decl,
                    TrackedTypes tracked_types);
  types::SchemeRef scheme;
  const ast::Decl *decl;
  TrackedTypes tracked_types;

  Location get_location() const;
};

} // namespace cider
