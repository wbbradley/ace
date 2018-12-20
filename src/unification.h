#pragma once
#include "ptr.h"
#include <memory>
#include <vector>
#include <map>
#include "types.h"

types::type_t::map unify(types::type_t::ref a, types::type_t::ref b);
types::type_t::map unify_many(std::vector<types::type_t::ref> as, std::vector<types::type_t::ref> b);
types::type_t::map compose(types::type_t::map a, types::type_t::map b);
bool type_equality(types::type_t::ref a, types::type_t::ref b);
