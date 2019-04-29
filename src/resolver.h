#pragma once

#include <memory>
#include <string>

#include "llvm_utils.h"
#include "types.h"

namespace gen {

enum resolution_status_t {
  rs_resolve_again,
  rs_cache_resolution,
};

typedef std::function<resolution_status_t(llvm::Value **)> lazy_resolver_callback_t;

struct publisher_t {
  virtual ~publisher_t() {
  }
  virtual void publish(llvm::Value *llvm_value) const = 0;
};

struct publishable_t : public publisher_t {
  publishable_t(llvm::Value **llvm_value);
  ~publishable_t();
  void publish(llvm::Value *llvm_value_) const override;

private:
  llvm::Value **llvm_value;
};

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
