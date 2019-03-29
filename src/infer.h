#pragma once
#include <tuple>
#include <vector>

#include "types.h"

struct constraint_info_t {
  std::string const reason;
  location_t const location;
  std::string str() const;
};

struct constraint_t {
  types::type_t::ref const a;
  types::type_t::ref const b;
  constraint_info_t const info;
  constraint_t rebind(const types::type_t::map &env) const;
  std::string str() const;
};
typedef std::vector<constraint_t> constraints_t;
types::type_t::ref infer(bitter::expr_t *expr,
                         env_t &env,
                         constraints_t &constraints);
std::string str(const constraints_t &constraints);
