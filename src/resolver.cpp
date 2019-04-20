#include "resolver.h"

#include <memory>
#include <string>

#include "resolver_impl.h"
#include "types.h"
#include "user_error.h"

namespace gen {

resolver_t::~resolver_t() {
}

llvm::Value *resolver_t::resolve() {
  try {
    return this->resolve_impl();
  } catch (user_error &e) {
    e.add_info(this->get_location(), "with %s", this->str().c_str());
    throw;
  }
}

std::shared_ptr<resolver_t> strict_resolver(llvm::Value *llvm_value) {
  return std::make_shared<strict_resolver_t>(llvm_value);
}

std::shared_ptr<resolver_t> lazy_resolver(std::string name,
                                          types::type_t::ref type,
                                          lazy_resolver_callback_t &&callback) {
  return std::make_shared<lazy_resolver_t>(name, type, std::move(callback));
}

publishable_t::publishable_t(llvm::Value **llvm_value)
    : llvm_value(llvm_value) {
}

publishable_t::~publishable_t() {
}

void publishable_t::publish(llvm::Value *llvm_value_) const {
  *llvm_value = llvm_value_;
}

} // namespace gen
