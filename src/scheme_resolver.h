#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "location.h"
#include "types.h"
#include "scheme.h"

typedef std::function<types::SchemeRef ()> SchemeResolverFn;
typedef std::shared_ptr<SchemeResolverFn> SchemeResolverFnRef;
typedef std::map<std::string, SchemeResolverFnRef> SchemeResolverMap;

struct SchemeResolver final {
  SchemeResolver() = default;
  SchemeResolver(const SchemeResolver &rhs) = default;
  ~SchemeResolver() = default;

  types::SchemeRef resolve(Location location, std::string name) const;
  void rebind(const types::Map &bindings) const;

  std::shared_ptr<SchemeResolverMap> map;
  types::Scheme::Map state;
};
