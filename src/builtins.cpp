#include "builtins.h"

#include "tld.h"

std::string ARROW_TYPE_OPERATOR = cider::tld::mktld("std", "->");
std::string PTR_TYPE_OPERATOR = cider::tld::mktld("std", "Ptr");
std::string REF_TYPE_OPERATOR = cider::tld::mktld("std", "Ref");

std::string CHAR_TYPE = "Char";
std::string INT_TYPE = "Int";
std::string UINT_TYPE = "UInt";
std::string INT64_TYPE = "Int64";
std::string UINT64_TYPE = "UInt64";
std::string INT32_TYPE = "Int32";
std::string UINT32_TYPE = "UInt32";
std::string INT16_TYPE = "Int16";
std::string UINT16_TYPE = "UInt16";
std::string INT8_TYPE = "Int8";
std::string UINT8_TYPE = "UInt8";
std::string FLOAT_TYPE = "Float";

std::string BOOL_TYPE = cider::tld::mktld("std", "Bool");
std::string MAYBE_TYPE = cider::tld::mktld("maybe", "Maybe");
std::string VECTOR_TYPE = cider::tld::mktld("vector", "Vector");
std::string MAP_TYPE = cider::tld::mktld("map", "Map");
std::string SET_TYPE = cider::tld::mktld("set", "Set");
std::string STRING_TYPE = cider::tld::mktld("string", "String");

const types::Scheme::Map &get_builtins() {
  static std::unique_ptr<types::Scheme::Map> map;
  if (map == nullptr) {
    auto Unit = type_unit(INTERNAL_LOC());
    auto Int = type_id(make_iid(INT_TYPE));
    auto Float = type_id(make_iid(FLOAT_TYPE));
    auto Bool = type_id(make_iid(BOOL_TYPE));
    auto Char = type_id(make_iid(CHAR_TYPE));
    auto String = type_id(make_iid(STRING_TYPE));
    auto PtrToChar = type_operator(type_id(make_iid(PTR_TYPE_OPERATOR)), Char);
    auto tv_a = type_variable(make_iid("a"));
    auto tp_a = type_ptr(tv_a);
    auto tv_b = type_variable(make_iid("b"));
    auto tp_b = type_ptr(tv_b);

    map = std::make_unique<types::Scheme::Map>();

    // TODO: unify this map with the implementation of these in gen.cpp
    (*map)["__builtin_hello"] = scheme(INTERNAL_LOC(), {}, {}, Unit);
    (*map)["__builtin_goodbye"] = scheme(INTERNAL_LOC(), {}, {}, Unit);
    (*map)["__builtin_word_size"] = scheme(INTERNAL_LOC(), {}, {}, Int);

    (*map)["__builtin_min_int"] = scheme(INTERNAL_LOC(), {}, {}, Int);
    (*map)["__builtin_max_int"] = scheme(INTERNAL_LOC(), {}, {}, Int);
    (*map)["__builtin_multiply_int"] = scheme(INTERNAL_LOC(), {}, {},
                                              type_arrows({Int, Int, Int}));
    (*map)["__builtin_divide_int"] = scheme(INTERNAL_LOC(), {}, {},
                                            type_arrows({Int, Int, Int}));
    (*map)["__builtin_subtract_int"] = scheme(INTERNAL_LOC(), {}, {},
                                              type_arrows({Int, Int, Int}));
    (*map)["__builtin_mod_int"] = scheme(INTERNAL_LOC(), {}, {},
                                         type_arrows({Int, Int, Int}));
    (*map)["__builtin_add_int"] = scheme(INTERNAL_LOC(), {}, {},
                                         type_arrows({Int, Int, Int}));
    (*map)["__builtin_negate_int"] = scheme(INTERNAL_LOC(), {}, {},
                                            type_arrows({Int, Int}));
    (*map)["__builtin_abs_int"] = scheme(INTERNAL_LOC(), {}, {},
                                         type_arrows({Int, Int}));
    (*map)["__builtin_multiply_char"] = scheme(INTERNAL_LOC(), {}, {},
                                               type_arrows({Char, Char, Char}));
    (*map)["__builtin_divide_char"] = scheme(INTERNAL_LOC(), {}, {},
                                             type_arrows({Char, Char, Char}));
    (*map)["__builtin_subtract_char"] = scheme(INTERNAL_LOC(), {}, {},
                                               type_arrows({Char, Char, Char}));
    (*map)["__builtin_add_char"] = scheme(INTERNAL_LOC(), {}, {},
                                          type_arrows({Char, Char, Char}));
    (*map)["__builtin_negate_char"] = scheme(INTERNAL_LOC(), {}, {},
                                             type_arrows({Char, Char}));
    (*map)["__builtin_abs_char"] = scheme(INTERNAL_LOC(), {}, {},
                                          type_arrows({Char, Char}));
    (*map)["__builtin_multiply_float"] = scheme(
        INTERNAL_LOC(), {}, {}, type_arrows({Float, Float, Float}));
    (*map)["__builtin_divide_float"] = scheme(
        INTERNAL_LOC(), {}, {}, type_arrows({Float, Float, Float}));
    (*map)["__builtin_subtract_float"] = scheme(
        INTERNAL_LOC(), {}, {}, type_arrows({Float, Float, Float}));
    (*map)["__builtin_add_float"] = scheme(INTERNAL_LOC(), {}, {},
                                           type_arrows({Float, Float, Float}));
    (*map)["__builtin_abs_float"] = scheme(INTERNAL_LOC(), {}, {},
                                           type_arrows({Float, Float}));
    (*map)["__builtin_int_to_float"] = scheme(INTERNAL_LOC(), {}, {},
                                              type_arrows({Int, Float}));
    (*map)["__builtin_float_to_int"] = scheme(INTERNAL_LOC(), {}, {},
                                              type_arrows({Float, Int}));
    (*map)["__builtin_negate_float"] = scheme(INTERNAL_LOC(), {}, {},
                                              type_arrows({Float, Float}));
    (*map)["__builtin_ptr_add"] = scheme(INTERNAL_LOC(), {"a"}, {},
                                         type_arrows({tp_a, Int, tp_a}));
    (*map)["__builtin_ptr_eq"] = scheme(INTERNAL_LOC(), {"a"}, {},
                                        type_arrows({tp_a, tp_a, Bool}));
    (*map)["__builtin_ptr_ne"] = scheme(INTERNAL_LOC(), {"a"}, {},
                                        type_arrows({tp_a, tp_a, Bool}));
    (*map)["__builtin_ptr_load"] = scheme(INTERNAL_LOC(), {"a"}, {},
                                          type_arrows({tp_a, tv_a}));
    (*map)["__builtin_get_dim"] = scheme(INTERNAL_LOC(), {"a", "b"}, {},
                                         type_arrows({tv_a, Int, tv_b}));
    (*map)["__builtin_cmp_ctor_id"] = scheme(INTERNAL_LOC(), {"a"}, {},
                                             type_arrows({tv_a, Int, Bool}));
    (*map)["__builtin_int_to_char"] = scheme(INTERNAL_LOC(), {}, {},
                                             type_arrows({Int, Char}));
    (*map)["__builtin_int_eq"] = scheme(INTERNAL_LOC(), {}, {},
                                        type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_ne"] = scheme(INTERNAL_LOC(), {}, {},
                                        type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_lt"] = scheme(INTERNAL_LOC(), {}, {},
                                        type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_lte"] = scheme(INTERNAL_LOC(), {}, {},
                                         type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_gt"] = scheme(INTERNAL_LOC(), {}, {},
                                        type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_gte"] = scheme(INTERNAL_LOC(), {}, {},
                                         type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_bitwise_and"] = scheme(INTERNAL_LOC(), {}, {},
                                                 type_arrows({Int, Int, Int}));
    (*map)["__builtin_int_bitwise_or"] = scheme(INTERNAL_LOC(), {}, {},
                                                type_arrows({Int, Int, Int}));
    (*map)["__builtin_int_bitwise_xor"] = scheme(INTERNAL_LOC(), {}, {},
                                                 type_arrows({Int, Int, Int}));
    (*map)["__builtin_int_bitwise_complement"] = scheme(
        INTERNAL_LOC(), {}, {}, type_arrows({Int, Int}));
    (*map)["__builtin_char_eq"] = scheme(INTERNAL_LOC(), {}, {},
                                         type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_ne"] = scheme(INTERNAL_LOC(), {}, {},
                                         type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_lt"] = scheme(INTERNAL_LOC(), {}, {},
                                         type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_lte"] = scheme(INTERNAL_LOC(), {}, {},
                                          type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_gt"] = scheme(INTERNAL_LOC(), {}, {},
                                         type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_gte"] = scheme(INTERNAL_LOC(), {}, {},
                                          type_arrows({Char, Char, Bool}));
    (*map)["__builtin_float_eq"] = scheme(INTERNAL_LOC(), {}, {},
                                          type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_ne"] = scheme(INTERNAL_LOC(), {}, {},
                                          type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_lt"] = scheme(INTERNAL_LOC(), {}, {},
                                          type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_lte"] = scheme(INTERNAL_LOC(), {}, {},
                                           type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_gt"] = scheme(INTERNAL_LOC(), {}, {},
                                          type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_gte"] = scheme(INTERNAL_LOC(), {}, {},
                                           type_arrows({Float, Float, Bool}));
    (*map)["__builtin_memcpy"] = scheme(
        INTERNAL_LOC(), {}, {},
        type_arrows({PtrToChar, PtrToChar, Int, type_unit(INTERNAL_LOC())}));
    (*map)["__builtin_memcmp"] = scheme(
        INTERNAL_LOC(), {}, {}, type_arrows({PtrToChar, PtrToChar, Int, Int}));
    (*map)["__builtin_pass_test"] = scheme(INTERNAL_LOC(), {}, {}, Unit);
    (*map)["__builtin_print_int"] = scheme(
        INTERNAL_LOC(), {}, {}, type_arrows({Int, type_unit(INTERNAL_LOC())}));
    (*map)["__builtin_calloc"] = scheme(INTERNAL_LOC(), {"a"}, {},
                                        type_arrows({Int, tp_a}));
    (*map)["__builtin_store_ref"] =
        scheme(INTERNAL_LOC(), {"a"}, {},
               type_arrows(
                   {type_operator(type_id(make_iid(REF_TYPE_OPERATOR)), tv_a),
                    tv_a, type_unit(INTERNAL_LOC())}))
            ->normalize();
    (*map)["__builtin_store_ptr"] =
        scheme(INTERNAL_LOC(), {"a"}, {},
               type_arrows(
                   {type_operator(type_id(make_iid(PTR_TYPE_OPERATOR)), tv_a),
                    tv_a, type_unit(INTERNAL_LOC())}))
            ->normalize();

    if (getenv("DUMP_BUILTINS") != nullptr &&
        atoi(getenv("DUMP_BUILTINS")) != 0) {
      for (auto &pair : *map) {
        if (starts_with(pair.first, "__builtin")) {
          log("%s :: %s", pair.first.c_str(), pair.second->str().c_str());
        }
      }
      std::exit(EXIT_SUCCESS);
    }
  }

  return *map;
}
