#include "resolver_impl.h"

#include <string>

#include "llvm_utils.h"
#include "user_error.h"

namespace gen {

strict_resolver_t::strict_resolver_t(llvm::Value *llvm_value)
    : llvm_value(llvm_value) {
}

strict_resolver_t::~strict_resolver_t() {
}

llvm::Value *strict_resolver_t::resolve_impl() {
  return llvm_value;
}

std::string strict_resolver_t::str() const {
  return llvm_print(llvm_value);
}

location_t strict_resolver_t::get_location() const {
  // TODO: plumbing
  return INTERNAL_LOC();
}
lazy_resolver_t::lazy_resolver_t(std::string name,
                                 types::type_t::ref type,
                                 lazy_resolver_callback_t &&callback)
    : sort_color(sc_unresolved), name(name), type(type),
      callback(std::move(callback)) {
}

lazy_resolver_t::~lazy_resolver_t() {
}

llvm::Value *lazy_resolver_t::resolve_impl() {
  // FUTURE: this is a good candidate for concurrency
  switch (sort_color) {
  case sc_unresolved:
    assert(value == nullptr);
    sort_color = sc_resolving;
    callback(&value);
    assert(value != nullptr);
    assert(llvm::dyn_cast<llvm::GlobalValue>(value) != nullptr);
    sort_color = sc_resolved;
    return value;
  case sc_resolving:
    /* we are already resolving this object, but progress on that front got far
     * enough that we can give back a value to be used elsewhere */
    if (value != nullptr) {
      assert(llvm::dyn_cast<llvm::GlobalValue>(value) != nullptr);
      return value;
    } else {
      throw user_error(
          INTERNAL_LOC(),
          "could not figure out how to resolve circular dependency");
    }
  case sc_resolved:
    assert(value != nullptr);
    assert(llvm::dyn_cast<llvm::GlobalValue>(value) != nullptr);
    return value;
  }
}

std::string lazy_resolver_t::str() const {
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

location_t lazy_resolver_t::get_location() const {
  // TODO: plumbing
  return INTERNAL_LOC();
}

} // namespace gen
