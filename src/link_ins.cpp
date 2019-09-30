#include "link_ins.h"

#include <iostream>

#include "user_error.h"

namespace zion {

LinkIn::LinkIn(const LinkIn &link_in) : lit(link_in.lit), name(link_in.name) {
}

LinkIn::LinkIn(LinkInType lit, Token name) : lit(lit), name(name) {
  for (auto ch: unescape_json_quotes(name.text)) {
    if (isalpha(ch) || isdigit(ch)) {
      continue;
    } else if (ch == '_' || ch == '-' || ch == '.') {
      continue;
    } else {
      throw user_error(name.location,
                       "illegal character '%c' encountered in link directive",
                       ch);
    }
  }
}

bool LinkIn::operator<(const LinkIn &rhs) const {
  return lit < rhs.lit || name < rhs.name;
}

} // namespace zion
