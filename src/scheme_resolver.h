#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "location.h"
#include "scheme.h"
#include "types.h"

namespace zion {

typedef std::function<types::SchemeRef()> SchemeResolverFn;
typedef std::shared_ptr<SchemeResolverFn> SchemeResolverFnRef;
typedef std::map<std::string, SchemeResolverFnRef> SchemeResolverMap;

struct SchemeResolver final {
  SchemeResolver() = default;
  SchemeResolver(const SchemeResolver &rhs) = delete;
  SchemeResolver(const SchemeResolver &&rhs) = delete;
  ~SchemeResolver() = default;

  void precache(std::string name, const types::SchemeRef &scheme);
  types::SchemeRef resolve(Location location, std::string name);
  void rebind(const types::Map &bindings);

  std::string str() const;

  SchemeResolverMap map;
  types::Scheme::Map state;
};

} // namespace zion
