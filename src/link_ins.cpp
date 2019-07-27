
#include "link_ins.h"

#include <iostream>

namespace zion {

LinkIn::LinkIn(const LinkIn &link_in) : lit(link_in.lit), name(link_in.name) {
}

LinkIn::LinkIn(LinkInType lit, Token name) : lit(lit), name(name) {
}

bool LinkIn::operator<(const LinkIn &rhs) const {
  return lit < rhs.lit || name < rhs.name;
}

} // namespace zion
