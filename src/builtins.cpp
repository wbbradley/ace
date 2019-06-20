#include "builtins.h"

const char *ARROW_TYPE_OPERATOR = "->";
const char *PTR_TYPE_OPERATOR = "*";
const char *REF_TYPE_OPERATOR = "std.Ref";

const char *CHAR_TYPE = "Char";
const char *INT_TYPE = "Int";
const char *UINT_TYPE = "UInt";
const char *INT64_TYPE = "Int64";
const char *UINT64_TYPE = "UInt64";
const char *INT32_TYPE = "Int32";
const char *UINT32_TYPE = "UInt32";
const char *INT16_TYPE = "Int16";
const char *UINT16_TYPE = "UInt16";
const char *INT8_TYPE = "Int8";
const char *UINT8_TYPE = "UInt8";
const char *FLOAT_TYPE = "Float";

const char *BOOL_TYPE = "std.Bool";
const char *MAYBE_TYPE = "std.Maybe";
const char *VECTOR_TYPE = "std.Vector";
const char *STRING_TYPE = "std.String";

const types::scheme_t::map &get_builtins() {
  static std::unique_ptr<types::scheme_t::map> map;
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

    map = std::make_unique<types::scheme_t::map>();

    // TODO: unify this map with the implementation of these in gen.cpp
    (*map)["__builtin_hello"] = scheme({}, {}, Unit);
    (*map)["__builtin_goodbye"] = scheme({}, {}, Unit);
    (*map)["__builtin_word_size"] = scheme({}, {}, Int);
    (*map)["__builtin_ffi_0"] = scheme({}, {}, type_arrows({PtrToChar, tv_a}));
    for (int i = 1; i <= 16; ++i) {
      types::type_t::refs terms{PtrToChar};
      for (int j = 0; j < i + 1; ++j) {
        terms.push_back(type_variable(INTERNAL_LOC()));
      }
      (*map)[string_format("__builtin_ffi_%d", i)] =
          scheme({}, {}, type_arrows(terms))->normalize();
    }

    (*map)["__builtin_min_int"] = scheme({}, {}, Int);
    (*map)["__builtin_max_int"] = scheme({}, {}, Int);
    (*map)["__builtin_multiply_int"] = scheme({}, {},
                                              type_arrows({Int, Int, Int}));
    (*map)["__builtin_divide_int"] = scheme({}, {},
                                            type_arrows({Int, Int, Int}));
    (*map)["__builtin_subtract_int"] = scheme({}, {},
                                              type_arrows({Int, Int, Int}));
    (*map)["__builtin_mod_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
    (*map)["__builtin_add_int"] = scheme({}, {}, type_arrows({Int, Int, Int}));
    (*map)["__builtin_negate_int"] = scheme({}, {}, type_arrows({Int, Int}));
    (*map)["__builtin_abs_int"] = scheme({}, {}, type_arrows({Int, Int}));
    (*map)["__builtin_multiply_char"] = scheme({}, {},
                                               type_arrows({Char, Char, Char}));
    (*map)["__builtin_divide_char"] = scheme({}, {},
                                             type_arrows({Char, Char, Char}));
    (*map)["__builtin_subtract_char"] = scheme({}, {},
                                               type_arrows({Char, Char, Char}));
    (*map)["__builtin_add_char"] = scheme({}, {},
                                          type_arrows({Char, Char, Char}));
    (*map)["__builtin_negate_char"] = scheme({}, {}, type_arrows({Char, Char}));
    (*map)["__builtin_abs_char"] = scheme({}, {}, type_arrows({Char, Char}));
    (*map)["__builtin_multiply_float"] = scheme(
        {}, {}, type_arrows({Float, Float, Float}));
    (*map)["__builtin_divide_float"] = scheme(
        {}, {}, type_arrows({Float, Float, Float}));
    (*map)["__builtin_subtract_float"] = scheme(
        {}, {}, type_arrows({Float, Float, Float}));
    (*map)["__builtin_add_float"] = scheme({}, {},
                                           type_arrows({Float, Float, Float}));
    (*map)["__builtin_abs_float"] = scheme({}, {}, type_arrows({Float, Float}));
    (*map)["__builtin_int_to_float"] = scheme({}, {},
                                              type_arrows({Int, Float}));
    (*map)["__builtin_negate_float"] = scheme({}, {},
                                              type_arrows({Float, Float}));
    (*map)["__builtin_ptr_add"] = scheme({"a"}, {},
                                         type_arrows({tp_a, Int, tp_a}));
    (*map)["__builtin_ptr_eq"] = scheme({"a"}, {},
                                        type_arrows({tp_a, tp_a, Bool}));
    (*map)["__builtin_ptr_ne"] = scheme({"a"}, {},
                                        type_arrows({tp_a, tp_a, Bool}));
    (*map)["__builtin_ptr_load"] = scheme({"a"}, {}, type_arrows({tp_a, tv_a}));
    (*map)["__builtin_get_dim"] = scheme({"a", "b"}, {},
                                         type_arrows({tv_a, Int, tv_b}));
    (*map)["__builtin_cmp_ctor_id"] = scheme({"a"}, {},
                                             type_arrows({tv_a, Int, Bool}));
    (*map)["__builtin_int_to_char"] = scheme({}, {}, type_arrows({Int, Char}));
    (*map)["__builtin_int_eq"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_ne"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_lt"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_lte"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_gt"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_gte"] = scheme({}, {}, type_arrows({Int, Int, Bool}));
    (*map)["__builtin_int_bitwise_and"] = scheme({}, {},
                                                 type_arrows({Int, Int, Int}));
    (*map)["__builtin_int_bitwise_or"] = scheme({}, {},
                                                type_arrows({Int, Int, Int}));
    (*map)["__builtin_int_bitwise_xor"] = scheme({}, {},
                                                 type_arrows({Int, Int, Int}));
    (*map)["__builtin_int_bitwise_complement"] = scheme(
        {}, {}, type_arrows({Int, Int}));
    (*map)["__builtin_char_eq"] = scheme({}, {},
                                         type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_ne"] = scheme({}, {},
                                         type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_lt"] = scheme({}, {},
                                         type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_lte"] = scheme({}, {},
                                          type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_gt"] = scheme({}, {},
                                         type_arrows({Char, Char, Bool}));
    (*map)["__builtin_char_gte"] = scheme({}, {},
                                          type_arrows({Char, Char, Bool}));
    (*map)["__builtin_float_eq"] = scheme({}, {},
                                          type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_ne"] = scheme({}, {},
                                          type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_lt"] = scheme({}, {},
                                          type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_lte"] = scheme({}, {},
                                           type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_gt"] = scheme({}, {},
                                          type_arrows({Float, Float, Bool}));
    (*map)["__builtin_float_gte"] = scheme({}, {},
                                           type_arrows({Float, Float, Bool}));
    (*map)["__builtin_memcpy"] = scheme(
        {}, {},
        type_arrows({PtrToChar, PtrToChar, Int, type_unit(INTERNAL_LOC())}));
    (*map)["__builtin_memcmp"] = scheme(
        {}, {}, type_arrows({PtrToChar, PtrToChar, Int, Int}));
    (*map)["__builtin_pass_test"] = scheme({}, {}, Unit);
    (*map)["__builtin_print_int"] = scheme(
        {}, {}, type_arrows({Int, type_unit(INTERNAL_LOC())}));
    (*map)["__builtin_itoa"] = scheme({}, {}, type_arrows({Int, PtrToChar}));
    (*map)["__builtin_ftoa"] = scheme({}, {}, type_arrows({Float, PtrToChar}));
    (*map)["__builtin_strlen"] = scheme({}, {}, type_arrows({PtrToChar, Int}));
    (*map)["__builtin_exit"] = scheme(
        {}, {}, type_arrows({Int, type_unit(INTERNAL_LOC())}));
    (*map)["__builtin_calloc"] = scheme({"a"}, {}, type_arrows({Int, tp_a}));
    (*map)["__builtin_store_ref"] =
        scheme({"a"}, {},
               type_arrows(
                   {type_operator(type_id(make_iid(REF_TYPE_OPERATOR)), tv_a),
                    tv_a, type_unit(INTERNAL_LOC())}))
            ->normalize();
    (*map)["__builtin_store_ptr"] =
        scheme({"a"}, {},
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
