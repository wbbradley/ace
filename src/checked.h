#pragma once

#include <list>
#include <memory>

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
  types::SchemeRef scheme;
  // forall a b . a -> [b]
  const ast::Decl *decl;
  TrackedTypes tracked_types;
};

} // namespace zion
