#pragma once
#include <tuple>
#include <vector>

#include "env.h"
#include "types.h"

struct constraint_info_t {
  std::string const reason;
  location_t const location;
  std::string str() const;
};

struct constraint_t {
  types::type_t::ref a;
  types::type_t::ref b;
  constraint_info_t info;
  void rebind(const types::type_t::map &env);
  std::string str() const;
};
typedef std::vector<constraint_t> constraints_t;
types::type_t::ref infer(bitter::expr_t *expr,
                         env_t &env,
                         constraints_t &constraints);
std::string str(const constraints_t &constraints);
