
#pragma once

#include "token.h"

enum LinkInType {
  lit_pkgconfig,
};

const char *littostr(LinkInType lit);

struct LinkIn {
  LinkIn() = delete;
  LinkIn(LinkInType lit, Token name);
  LinkIn(const LinkIn &lit);
  LinkInType lit;

  Token name;
  bool operator<(const LinkIn &) const;
};
