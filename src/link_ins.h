
#pragma once

#include "token.h"

namespace cider {

enum LinkInType {
  lit_pkgconfig,
  lit_link,
  lit_compile,
};

struct LinkIn {
  LinkIn() = delete;
  LinkIn(LinkInType lit, Token name);
  LinkIn(const LinkIn &lit);
  LinkInType lit;

  Token name;
  bool operator<(const LinkIn &) const;
};

} // namespace cider
