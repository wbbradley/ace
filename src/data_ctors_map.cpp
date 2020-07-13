#include "data_ctors_map.h"

#include <sstream>
#include <string>

#include "dbg.h"
#include "logger_decls.h"
#include "ptr.h"
#include "user_error.h"

namespace zion {

types::Ref get_data_ctor_type(const DataCtorsMap &data_ctors_map,
                              types::Ref type,
                              const Identifier &ctor_id) {
  types::Refs type_terms;
  unfold_ops_lassoc(type, type_terms);
  assert(type_terms.size() != 0);

  auto id = safe_dyncast<const types::TypeId>(type_terms[0]);
  debug_above(7, log("looking for %s in data_ctors_map of size %d",
                     id->str().c_str(),
                     int(data_ctors_map.data_ctors_type_map.size())));
  debug_above(8, log("%s", ::str(data_ctors_map).c_str()));
  if (data_ctors_map.data_ctors_type_map.count(id->id.name) == 0) {
    throw user_error(id->get_location(),
                     "could not find a data ctor type for %s",
                     id->str().c_str());
  }
  auto &data_ctors = data_ctors_map.data_ctors_type_map.at(id->id.name);

  auto ctor_type = get(data_ctors, ctor_id.name, {});
  if (ctor_type == nullptr) {
    throw user_error(ctor_id.location, "data ctor %s does not exist",
                     ctor_id.str().c_str());
  }

  debug_above(7, log("starting with ctor_type as %s and type_terms as %s",
                     ctor_type->str().c_str(), ::str(type_terms).c_str()));

  for (size_t i = 1; i < type_terms.size(); ++i) {
    ctor_type = ctor_type->apply(type_terms[i]);
  }
  debug_above(7, log("resolved ctor_type as %s", ctor_type->str().c_str()));

  return ctor_type;
}

std::map<std::string, types::Ref> get_data_ctors_types(
    const DataCtorsMap &data_ctors_map,
    types::Ref type) {
  std::cerr << "unfolding " << type->str() << std::endl;
  types::Refs type_terms;
  unfold_ops_lassoc(type, type_terms);
  assert(type_terms.size() != 0);

  auto id = safe_dyncast<const types::TypeId>(type_terms[0]);
  debug_above(7, log("looking for %s in data_ctors_map of size %d",
                     id->str().c_str(),
                     int(data_ctors_map.data_ctors_type_map.size())));
  debug_above(7, log("%s", ::str(data_ctors_map).c_str()));

  if (data_ctors_map.data_ctors_type_map.count(id->id.name) == 0) {
    throw user_error(id->id.location,
                     "ICE: unable to find ctor %s in data_ctors_type_map",
                     id->str().c_str());
  }
  auto &data_ctors = data_ctors_map.data_ctors_type_map.at(id->id.name);

  std::map<std::string, types::Ref> data_ctors_types;

  for (auto pair : data_ctors) {
    auto ctor_type = pair.second;
    debug_above(7, log("starting with ctor_type as %s and type_terms as %s",
                       ctor_type->str().c_str(), ::str(type_terms).c_str()));

    for (size_t i = 1; i < type_terms.size(); ++i) {
      ctor_type = ctor_type->apply(type_terms[i]);
    }
    debug_above(7, log("resolved ctor_type as %s", ctor_type->str().c_str()));

    data_ctors_types[pair.first] = ctor_type;
  }
  return data_ctors_types;
}

int get_ctor_id(const DataCtorsMap &data_ctors_map, std::string ctor_name) {
  auto iter = data_ctors_map.ctor_id_map.find(ctor_name);
  if (iter == data_ctors_map.ctor_id_map.end()) {
    // dbg();
    auto error = user_error(INTERNAL_LOC(),
                            "bad ctor name requested during translation (%s)",
                            ctor_name.c_str());
    for (auto pair : data_ctors_map.ctor_id_map) {
      error.add_info(INTERNAL_LOC(), "it's not %s", pair.first.c_str());
    }
    throw error;
  } else {
    return iter->second;
  }
}

types::Ref get_fresh_data_ctor_type(const DataCtorsMap &data_ctors_map,
                                    Identifier ctor_id) {
  // FUTURE: build an index to make this faster
  for (auto type_ctors : data_ctors_map.data_ctors_type_map) {
    for (auto ctors : type_ctors.second) {
      if (ctors.first == ctor_id.name) {
        types::Ref ctor_type = ctors.second;
        while (true) {
          if (auto type_lambda = dyncast<const types::TypeLambda>(ctor_type)) {
            ctor_type = type_lambda->apply(type_variable(INTERNAL_LOC()));
          } else {
            return ctor_type;
          }
        }
      }
    }
  }

  throw user_error(ctor_id.location, "no data constructor found for %s",
                   ctor_id.str().c_str());
}

} // namespace zion

std::string str(const zion::DataCtorsMap &data_ctors_map) {
  std::stringstream ss;
  const char *delim = "";
  for (auto pair : data_ctors_map.data_ctors_type_map) {
    ss << delim << pair.first << ": " << ::str(pair.second);
    delim = ", ";
  }
  return ss.str();
}
