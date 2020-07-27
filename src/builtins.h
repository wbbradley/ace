#pragma once

#include <map>
#include <memory>
#include <string>

#include "scheme.h"

extern std::string MAYBE_TYPE;
extern std::string INT_TYPE;
extern std::string UINT_TYPE;
extern std::string INT64_TYPE;
extern std::string UINT64_TYPE;
extern std::string INT32_TYPE;
extern std::string UINT32_TYPE;
extern std::string INT16_TYPE;
extern std::string UINT16_TYPE;
extern std::string INT8_TYPE;
extern std::string UINT8_TYPE;
extern std::string CHAR_TYPE;
extern std::string BOOL_TYPE;
extern std::string FLOAT_TYPE;
extern std::string MBS_TYPE;
extern std::string PTR_TO_MBS_TYPE;
extern std::string TYPEID_TYPE;
extern std::string ARROW_TYPE_OPERATOR;
extern std::string PTR_TYPE_OPERATOR;
extern std::string REF_TYPE_OPERATOR;
extern std::string VECTOR_TYPE;
extern std::string MAP_TYPE;
extern std::string SET_TYPE;
extern std::string STRING_TYPE;

const types::SchemeMap &get_builtins();
