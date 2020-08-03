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

llvm::Value *StrictResolver::resolve_impl(llvm::IRBuilder<> &builder, Location location) {
  return llvm_value;
}

std::string StrictResolver::str() const {
  return llvm_print(llvm_value);
}

LazyResolver::LazyResolver(std::string name,
                           types::Ref type,
                           LazyResolverCallback &&callback)
    : sort_color(sc_unresolved), name(name), type(type),
      callback(std::move(callback)) {
}

LazyResolver::~LazyResolver() {
}

llvm::Value *LazyResolver::resolve_impl(llvm::IRBuilder<> &builder, Location location) {
  // FUTURE: this is a good candidate for concurrency
  switch (sort_color) {
  case sc_unresolved:
    assert(value == nullptr);
    sort_color = sc_resolving;
    switch (callback(&value)) {
    case rs_resolve_again: {
      sort_color = sc_unresolved;
      assert(value != nullptr);
      auto ret_value = value;
      /* this is set to resolve again, so forget the value */
      value = nullptr;
      debug_above(
          5, log("LazyResolver resolved %s", llvm_print(ret_value).c_str()));
      return ret_value;
    }
    case rs_cache_resolution: {
      assert(value != nullptr);
      sort_color = sc_resolved;
      debug_above(5,
                  log("LazyResolver resolved %s", llvm_print(value).c_str()));
      return value;
    }
    case rs_cache_global_load: {
      assert(value != nullptr);
      assert(llvm::dyn_cast<llvm::GlobalVariable>(value) != nullptr);
      sort_color = sc_resolved_with_global_reload;
      debug_above(5, log("LazyResolver resolved %s and is reloading",
                         llvm_print(value).c_str()));
      llvm::Instruction *load = builder.CreateLoad(value);
      load->setName(string_format("loading %s at %s", name.c_str(),
                                  location.str().c_str()));
      return load;
    }
    }
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
  case sc_resolved_with_global_reload:
    assert(value != nullptr);
    assert(llvm::dyn_cast<llvm::GlobalVariable>(value) != nullptr);
    llvm::Instruction *load = builder.CreateLoad(value);
    load->setName(string_format("loading %s at %s", name.c_str(),
                                location.str().c_str()));
    return load;
  }
  assert(false);
  return nullptr;
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
  case sc_resolved_with_global_reload:
    return string_format("resolved (loading global) " c_id("%s") " :: %s to %s", name.c_str(),
                         type->str().c_str(), llvm_print(value).c_str());
  }
  assert(false);
  return {};
}

} // namespace gen

} // namespace zion
