#pragma once
#include <ostream>
#include <string>

#include "identifier.h"

namespace types {
struct scheme_t;
struct type_t;
} // namespace types

struct defn_id_t {
  defn_id_t(identifier_t const id,
            std::shared_ptr<types::scheme_t> const scheme)
      : id(id), scheme(scheme) {
  }

  identifier_t const id;
  std::shared_ptr<types::scheme_t> const scheme;

  /* convert all free type variables to type unit */
  defn_id_t unitize() const;

private:
  mutable std::string cached_repr;
  std::string repr() const;
  identifier_t repr_id() const;

public:
  std::string repr_public() const {
    return repr();
  }
  location_t get_location() const;
  std::string str() const;
  bool operator<(const defn_id_t &rhs) const;
  std::shared_ptr<const types::type_t> get_lambda_param_type() const;
  std::shared_ptr<const types::type_t> get_lambda_return_type() const;
};

std::ostream &operator<<(std::ostream &os, const defn_id_t &defn_id);
