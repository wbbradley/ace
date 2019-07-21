#include "resolver_impl.h"

#include <string>

#include "dbg.h"
#include "llvm_utils.h"
#include "user_error.h"

namespace zion {

namespace gen {

StrictResolver::StrictResolver(llvm::Value *llvm_value)
    : llvm_value(llvm_value) {
}

StrictResolver::~StrictResolver() {
}

llvm::Value *StrictResolver::resolve_impl() {
  return llvm_value;
}

std::string StrictResolver::str() const {
  return llvm_print(llvm_value);
}

Location StrictResolver::get_location() const {
  // TODO: plumbing
  return INTERNAL_LOC();
}

LazyResolver::LazyResolver(std::string name,
                           types::Ref type,
                           LazyResolverCallback &&callback)
    : sort_color(sc_unresolved), name(name), type(type),
      callback(std::move(callback)) {
}

LazyResolver::~LazyResolver() {
}

llvm::Value *LazyResolver::resolve_impl() {
  // FUTURE: this is a good candidate for concurrency
  switch (sort_color) {
  case sc_unresolved:
    assert(value == nullptr);
    sort_color = sc_resolving;
    switch (callback(&value)) {
    case rs_resolve_again:
      sort_color = sc_unresolved;
      break;
    case rs_cache_resolution:
      sort_color = sc_resolved;
      break;
    }
    assert(value != nullptr);
    debug_above(5, log("LazyResolver resolved %s", llvm_print(value).c_str()));
    return value;
  case sc_resolving:
    /* we are already resolving this object, but progress on that front got far
     * enough that we can give back a value to be used elsewhere */
    if (value != nullptr) {
      return value;
    } else {
      throw user_error(
          INTERNAL_LOC(),
          "could not figure out how to resolve circular dependency");
    }
  case sc_resolved:
    assert(value != nullptr);
    return value;
  }
}

std::string LazyResolver::str() const {
  switch (sort_color) {
  case sc_unresolved:
    return string_format("unresolved " c_id("%s") " :: %s", name.c_str(),
                         type->str().c_str());
  case sc_resolving:
    /* we are already resolving this object, but progress on that front got far
     * enough that we can give back a value to be used elsewhere */
    if (value != nullptr) {
      return string_format(
          "resolving " c_id("%s") " :: %s (partially resolved to %s)",
          name.c_str(), type->str().c_str(), llvm_print(value).c_str());
    } else {
      return string_format("resolving " c_id("%s") " :: %s", name.c_str(),
                           type->str().c_str());
    }
  case sc_resolved:
    return string_format("resolved " c_id("%s") " :: %s to %s", name.c_str(),
                         type->str().c_str(), llvm_print(value).c_str());
  }
}

Location LazyResolver::get_location() const {
  // TODO: plumbing
  return INTERNAL_LOC();
}

} // namespace gen

} // namespace zion
