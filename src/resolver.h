#pragma once

#include <string>
#include <memory>

#include "llvm_utils.h"
#include "types.h"

namespace gen {

using lazy_resolver_callback_t = std::function<void(llvm::Value **)>;

struct resolver_t {
  virtual ~resolver_t() = 0;
  llvm::Value *resolve();
  virtual llvm::Value *resolve_impl() = 0;
  virtual std::string str() const = 0;
  virtual location_t get_location() const = 0;
};

std::shared_ptr<resolver_t> strict_resolver(llvm::Value *llvm_value);
std::shared_ptr<resolver_t> lazy_resolver(std::string name,
                                          types::type_t::ref type,
                                          lazy_resolver_callback_t &&callback);

} // namespace gen
