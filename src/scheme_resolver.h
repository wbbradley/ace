#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "identifier.h"
#include "location.h"
#include "scheme.h"
#include "types.h"

namespace types {

struct SchemeResolver final {
  SchemeResolver() = default;
  SchemeResolver(const SchemeResolver &rhs) = delete;
  SchemeResolver(SchemeResolver &&rhs) = default;
  ~SchemeResolver() = default;

  bool scheme_exists(std::string name) const;
  void insert_scheme(std::string name, const types::SchemeRef &scheme);
  types::SchemeRef lookup_scheme(const Identifier &id) const;
  void rebind(const types::Map &bindings) const;

  std::string str() const;

private:
  types::Scheme::Map state;
};

} // namespace types
