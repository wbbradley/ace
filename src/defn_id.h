#pragma once
#include <ostream>
#include <string>

#include "identifier.h"

namespace types {
struct Scheme;
struct Type;
} // namespace types

struct DefnId {
  DefnId(Identifier const id, std::shared_ptr<types::Scheme> const scheme)
      : id(id), scheme(scheme) {
  }

  Identifier const id;
  std::shared_ptr<types::Scheme> const scheme;

  /* convert all free type variables to type unit */
  DefnId unitize() const;

private:
  mutable std::string cached_repr;
  std::string repr() const;
  Identifier repr_id() const;

public:
  std::string repr_public() const {
    return repr();
  }
  Location get_location() const;
  std::string str() const;
  bool operator<(const DefnId &rhs) const;
  std::shared_ptr<const types::Type> get_lambda_param_type() const;
  std::shared_ptr<const types::Type> get_lambda_return_type() const;
};

std::ostream &operator<<(std::ostream &os, const DefnId &defn_id);
