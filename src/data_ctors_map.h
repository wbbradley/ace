#pragma once

#include "identifier.h"
#include "types.h"

namespace zion {

typedef std::map<std::string, types::Map> ParsedDataCtorsMap;
typedef std::unordered_map<std::string, int> ParsedCtorIdMap;

struct DataCtorsMap {
  ParsedDataCtorsMap const data_ctors_type_map;
  ParsedCtorIdMap const ctor_id_map;
};

types::Map get_data_ctors_types(const DataCtorsMap &data_ctors_map,
                                types::Ref type);
types::Ref get_data_ctor_type(const DataCtorsMap &data_ctors_map,
                              types::Ref type,
                              const Identifier &ctor_id);
int get_ctor_id(Location location, const DataCtorsMap &data_ctors_map, std::string ctor_name);

types::Ref get_fresh_data_ctor_type(const DataCtorsMap &data_ctors_map,
                                    Identifier ctor_id);

} // namespace zion

std::string str(const zion::DataCtorsMap &data_ctors_map);
