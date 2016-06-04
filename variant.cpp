/*
 *  variant.cpp
 *
 *  Created by Will Bradley on 2/27/11.
 *  Copyright 2011, 2015, 2016 Will Bradley.
 *
 */
#include "dbg.h"
#include "variant.h"
#include <sstream>
#include <iomanip>
#include <iostream>
#include "json_parser.h"
#include <stdint.h>
#include "logger_decls.h"
#include "utils.h"

const std::string escape_json_quotes(const std::string &str);

const runtime::variant vt_null;

namespace runtime {

	const char *vk_to_str(variant_kind vk) {
		switch (vk) {
		case vk_char:
			return "char";
		case vk_int:
			return "int";
		case vk_uint:
			return "uint";
		case vk_float:
			return "float";
		case vk_double:
			return "double";
		case vk_bool:
			return "bool";
		case vk_null:
			return "null";
		case vk_str:
			return "str";
		case vk_reference:
			return "reference";
		case vk_vector:
			return "vector";
		case vk_hash_map:
			return "hash_map";
		case vk_buffer:
			return "buffer";
		}
		assert(false);
		return "null";
	}

	bool vk_from_str(const std::string &kind, variant_kind &vk) {
		if (kind == "bool")
			vk = vk_bool;
		else if (kind == "str")
			vk = vk_str;
		else if (kind == "null")
			vk = vk_null;
		else if (kind == "float")
			vk = vk_float;
		else if (kind == "double")
			vk = vk_double;
		else if (kind == "int")
			vk = vk_int;
		else if (kind == "uint")
			vk = vk_uint;
		else if (kind == "vector")
			vk = vk_vector;
		else if (kind == "hash_map")
			vk = vk_hash_map;
		else if (kind == "buffer")
			vk = vk_buffer;
		else {
			assert(false);
			return false;
		}
		return true;
	}

	variant::variant(char val) : kind(vk_char), char_val(val) {
	}

	variant::variant(const char *val) : kind(vk_str), str_val(val) {
	}

	variant::variant(const std::string &val) : kind(vk_str), str_val(val) {
	}

	variant::variant(int val) : kind(vk_int), int_val(val) {
	}

	variant::variant(unsigned int val) : kind(vk_uint), uint_val(val) {
	}

	variant::variant(int64_t val) : kind(vk_int), int_val(val) {
	}

	variant::variant(uint64_t val) : kind(vk_uint), uint_val(val) {
	}

	variant::variant(float val) : kind(vk_float), float_val(val) {
	}

	variant::variant(double val) : kind(vk_double), double_val(val) {
	}

	variant::variant(bool val) : kind(vk_bool), bool_val(val) {
	}

	variant::variant(const ptr<variant> &val) : kind(vk_reference), reference_val(val) {
	}

	variant::variant(const std::vector<variant> &val) : kind(vk_vector), vector_val(val) {
	}

	variant::variant(const ptr<std::unordered_map<std::string, variant>> &val) : kind(vk_hash_map), hash_map_val(val) {
	}

	variant::variant(char *buffer_ptr, size_t size) : kind(vk_buffer), buffer_ptr_val(buffer_ptr), buffer_size_val(size) {
	}

	std::string variant::str(bool resolve_references) const {
		if (resolve_references && kind == vk_reference) {
			if (reference_val) {
				return reference_val->str(resolve_references);
			} else {
				return "<null reference>";
			}
		} else {
			std::stringstream ss;
			write_as_json(ss);
			return ss.str();
		}
	}

	void variant::write_as_json(std::ostream &os) const {
		switch (kind) {
		case vk_char:
			os << char_val;
			return;
		case vk_uint:
			os << uint_val;
			return;
		case vk_int:
			os << int_val;
			return;
		case vk_float:
			os << std::fixed << std::setprecision(3) << float_val;
			return;
		case vk_str:
			escape_json_quotes(os, str_val);
			return;
		case vk_bool:
			os << boolstr(bool_val);
			return;
		case vk_double:
			os << std::fixed << std::setprecision(3) << double_val;
			return;
		case vk_null:
			os << "null";
			return;
		case vk_reference:
			os << "{\"value\": ";
			if (reference_val) {
				reference_val->write_as_json(os);
			} else {
				os << "null";
			}
			os << "}";
			return;
		case vk_vector:
			{
				os << "[";
				const char *sep = "";
				for (auto &x : vector_val) {
					os << sep;
					x.write_as_json(os);
					sep = ", ";
				}
				os << "]";
				return;
			}

		case vk_hash_map:
			{
				os << "{";
				const char *sep = "";
				for (auto it = hash_map_val->begin(); it != hash_map_val->end(); ++it) {
					os << sep << escape_json_quotes(it->first) << ": ";
					it->second.write_as_json(os);
					sep = ", ";
				}			
				os << "}";
				return;
			}
		case vk_buffer:
			{
				std::string encoded;
				base64_encode((void *)buffer_ptr_val, buffer_size_val, encoded);
			}
		}
		os << "null";
	}

	bool variant::operator ==(const variant &rhs) const {
		if (kind != rhs.kind)
			return false;

		switch (kind) {
		case vk_char:
			return char_val == rhs.char_val;
		case vk_int:
			return int_val == rhs.int_val;
		case vk_uint:
			return uint_val == rhs.uint_val;
		case vk_float:
			return float_val == rhs.float_val;
		case vk_str:
			return str_val == rhs.str_val;
		case vk_bool:
			return bool_val == rhs.bool_val;
		case vk_double:
			return double_val == rhs.double_val;
		case vk_null:
			return true;
		case vk_reference:
			return reference_val == rhs.reference_val;
		case vk_vector:
			return vector_val == rhs.vector_val;
		case vk_hash_map:
			return hash_map_val == rhs.hash_map_val;
		case vk_buffer:
			return buffer_ptr_val == rhs.buffer_ptr_val;
		}
	}

	bool variant::operator !=(const variant &rhs) const {
		if (kind != rhs.kind)
			return true;

		switch (kind) {
		case vk_char:
			return char_val != rhs.char_val;
		case vk_int:
			return int_val != rhs.int_val;
		case vk_uint:
			return uint_val != rhs.uint_val;
		case vk_float:
			return float_val != rhs.float_val;
		case vk_str:
			return str_val != rhs.str_val;
		case vk_bool:
			return bool_val != rhs.bool_val;
		case vk_double:
			return double_val != rhs.double_val;
		case vk_null:
			return false;
		case vk_reference:
			return reference_val != rhs.reference_val;
		case vk_vector:
			return vector_val != rhs.vector_val;
		case vk_hash_map:
			return hash_map_val != rhs.hash_map_val;
		case vk_buffer:
			return buffer_ptr_val != rhs.buffer_ptr_val;
		}
	}

	bool variant::operator <(const variant &rhs) const {
		if (kind != rhs.kind) {
			assert(false);
			return false;
		}

		switch (kind) {
		case vk_char:
			return char_val < rhs.char_val;
		case vk_int:
			return int_val < rhs.int_val;
		case vk_uint:
			return uint_val < rhs.uint_val;
		case vk_float:
			return float_val < rhs.float_val;
		case vk_str:
			return str_val < rhs.str_val;
		case vk_bool:
			assert(false);
			return bool_val < rhs.bool_val;
		case vk_double:
			return double_val < rhs.double_val;
		case vk_null:
			assert(false);
			return false;
		case vk_reference:
			return reference_val < rhs.reference_val;
		case vk_vector:
			assert(false);
			return false;
		case vk_hash_map:
			assert(false);
			return false;
		case vk_buffer:
			return buffer_ptr_val < rhs.buffer_ptr_val;
		}
	}

	bool variant::operator <=(const variant &rhs) const {
		if (kind != rhs.kind) {
			assert(false);
			return false;
		}

		switch (kind) {
		case vk_char:
			return char_val <= rhs.char_val;
		case vk_int:
			return int_val <= rhs.int_val;
		case vk_uint:
			return uint_val <= rhs.uint_val;
		case vk_float:
			return float_val <= rhs.float_val;
		case vk_str:
			return str_val <= rhs.str_val;
		case vk_bool:
			assert(false);
			return bool_val <= rhs.bool_val;
		case vk_double:
			return double_val <= rhs.double_val;
		case vk_null:
			assert(false);
			return false;
		case vk_reference:
			return reference_val <= rhs.reference_val;
		case vk_vector:
			assert(false);
			return false;
		case vk_hash_map:
			assert(false);
			return false;
		case vk_buffer:
			return buffer_ptr_val <= rhs.buffer_ptr_val;
		}
	}

	bool variant::operator >(const variant &rhs) const {
		if (kind != rhs.kind) {
			assert(false);
			return false;
		}

		switch (kind) {
		case vk_char:
			return char_val > rhs.char_val;
		case vk_int:
			return int_val > rhs.int_val;
		case vk_uint:
			return uint_val > rhs.uint_val;
		case vk_float:
			return float_val > rhs.float_val;
		case vk_str:
			return str_val > rhs.str_val;
		case vk_bool:
			assert(false);
			return bool_val > rhs.bool_val;
		case vk_double:
			return double_val > rhs.double_val;
		case vk_null:
			assert(false);
			return false;
		case vk_reference:
			return reference_val > rhs.reference_val;
		case vk_vector:
			assert(false);
			return false;
		case vk_hash_map:
			assert(false);
			return false;
		case vk_buffer:
			return buffer_ptr_val > rhs.buffer_ptr_val;
		}
	}

	bool variant::operator >=(const variant &rhs) const {
		if (kind != rhs.kind) {
			assert(false);
			return false;
		}

		switch (kind) {
		case vk_char:
			return char_val >= rhs.char_val;
		case vk_int:
			return int_val >= rhs.int_val;
		case vk_uint:
			return uint_val >= rhs.uint_val;
		case vk_float:
			return float_val >= rhs.float_val;
		case vk_str:
			return str_val >= rhs.str_val;
		case vk_bool:
			assert(false);
			return bool_val >= rhs.bool_val;
		case vk_double:
			return double_val >= rhs.double_val;
		case vk_null:
			assert(false);
			return false;
		case vk_reference:
			return reference_val >= rhs.reference_val;
		case vk_vector:
			assert(false);
			return false;
		case vk_hash_map:
			assert(false);
			return false;
		case vk_buffer:
			return buffer_ptr_val >= rhs.buffer_ptr_val;
		}
	}

	bool convert_variant(const variant &vt, variant &val) {
		val = vt;
		return true;
	}

	bool convert_variant(const variant &vt, std::string &str) {
		if (vt.kind == vk_null) {
			debug(log(log_info, "convert_variant : warning : attempt to convert vk_null to string\n"));
			return false;
		}
		if (vt.kind == vk_str) {
			str = vt.str_val;
			return true;
		}

#ifdef ZION_DEBUG
		std::cerr << "convert_variant : warning : couldn't convert ";
		std::cerr << vk_to_str(vt.kind) << " ";
		debug(vt.write_as_json(std::cerr));
		std::cerr << " to string\n";
#endif

		return false;
	}

	bool convert_variant(const variant &vt, uint32_t &val) {
		if (vt.kind == vk_null) {
			debug(log(log_info, "convert_variant : warning : attempt to convert vk_null to uint32_t\n"));
			return false;
		}
		if (vt.kind == vk_uint) {
			val = (uint32_t)vt.uint_val;
			return true;
		}
#ifdef ZION_DEBUG
		std::cerr << "convert_variant : warning : couldn't convert ";
		std::cerr << vk_to_str(vt.kind) << " ";
		debug(vt.write_as_json(std::cerr));
		std::cerr << " to uint32_t\n";
#endif
		return false;
	}

	bool convert_variant(const variant &vt, int32_t &val) {
		if (vt.kind == vk_null) {
			debug(log(log_info, "convert_variant : warning : attempt to convert vk_null to int32_t\n"));
			return false;
		}
		if (vt.kind == vk_int) {
			val = (int32_t)vt.int_val;
			return true;
		}
#ifdef ZION_DEBUG
		std::cerr << "convert_variant : warning : couldn't convert ";
		std::cerr << vk_to_str(vt.kind) << " ";
		debug(vt.write_as_json(std::cerr));
		std::cerr << " to int32_t\n";
#endif
		return false;
	}

	bool convert_variant(const variant &vt, uint64_t &val) {
		switch (vt.kind) {
		case vk_null:
			debug(log(log_info, "convert_variant : warning : attempt to convert vk_null to uint64_t\n"));
			return false;
		case vk_uint:
			val = vt.uint_val;
			return true;
		case vk_int:
			if (vt.int_val >= 0) {
				val = vt.int_val;
				return true;
			} else {
				std::cerr << "convert_variant : warning : couldn't convert ";
				std::cerr << vk_to_str(vt.kind) << " ";
				debug(vt.write_as_json(std::cerr));
				std::cerr << " to uint64_t\n";
				return false;
			}
		default:
#ifdef ZION_DEBUG
			std::cerr << "convert_variant : warning : couldn't convert ";
			std::cerr << vk_to_str(vt.kind) << " ";
			debug(vt.write_as_json(std::cerr));
			std::cerr << " to uint64_t\n";
#endif
			return false;
		}
	}

	bool convert_variant(const variant &vt, int64_t &val) {
		if (vt.kind == vk_null) {
			debug(log(log_info, "convert_variant : warning : attempt to convert vk_null to int64_t\n"));
			return false;
		}
		if (vt.kind == vk_int) {
			val = vt.int_val;
			return true;
		}
#ifdef ZION_DEBUG
		std::cerr << "convert_variant : warning : couldn't convert ";
		std::cerr << vk_to_str(vt.kind) << " ";
		debug(vt.write_as_json(std::cerr));
		std::cerr << " to int64_t\n";
#endif
		return false;
	}

	bool convert_variant(const variant &vt, uint8_t &val) {
		if (vt.kind == vk_null) {
			debug(log(log_info, "convert_variant : warning : attempt to convert vk_null to uint8_t\n"));
			return false;
		}
		if ((vt.kind == vk_uint) && (vt.uint_val <= 255)) {
			val = vt.uint_val;
			return true;
		}
#ifdef ZION_DEBUG
		std::cerr << "convert_variant : warning : couldn't convert ";
		std::cerr << vk_to_str(vt.kind) << " ";
		debug(vt.write_as_json(std::cerr));
		std::cerr << " to uint8_t\n";
#endif
		return false;
	}

	bool convert_variant(const variant &vt, int8_t &val) {
		if (vt.kind == vk_null) {
			debug(log(log_info, "convert_variant : warning : attempt to convert vk_null to int8_t\n"));
			return false;
		}
		if ((vt.kind == vk_int) && (vt.int_val >= -128) && (vt.int_val <= 127)) {
			val = vt.int_val;
			return true;
		}
#ifdef ZION_DEBUG
		std::cerr << "convert_variant : warning : couldn't convert ";
		std::cerr << vk_to_str(vt.kind) << " ";
		debug(vt.write_as_json(std::cerr));
		std::cerr << " to int8_t\n";
#endif
		return false;
	}

	bool convert_variant(const variant &vt, float &val) {
		if (vt.kind == vk_null) {
			debug(log(log_info, "convert_variant : warning : attempt to convert vk_null to float\n"));
			return false;
		}
		if (vt.kind == vk_float) {
			val = vt.float_val;
			return true;
		}
		if (vt.kind == vk_double) {
			val = vt.double_val;
			if (val == val) {
				return true;
			} else {
#ifdef ZION_DEBUG
				std::cerr << "double precision number truncated by conversion to float\n";
				return false;
#endif
			}
		}
#ifdef ZION_DEBUG
		std::cerr << "convert_variant : warning : couldn't convert ";
		std::cerr << vk_to_str(vt.kind) << " ";
		debug(vt.write_as_json(std::cerr));
		std::cerr << " to float\n";
#endif
		return false;
	}

	bool convert_variant(const variant &vt, double &val) {
		if (vt.kind == vk_null) {
			debug(log(log_info, "convert_variant : warning : attempt to convert vk_null to double\n"));
			return false;
		}
		if (vt.kind == vk_double) {
			val = vt.double_val;
			return true;
		}
#ifdef ZION_DEBUG
		std::cerr << "convert_variant : warning : couldn't convert ";
		std::cerr << vk_to_str(vt.kind) << " ";
		debug(vt.write_as_json(std::cerr));
		std::cerr << " to double\n";
#endif
		return false;
	}

	bool convert_variant(const variant &vt, bool &val) {
		switch (vt.kind) {
		case vk_char:
			val = vt.char_val;
			return true;
		case vk_null:
			val = false;
			return true;
		case vk_bool:
			val = vt.bool_val;
			return true;
		case vk_int:
			val = vt.int_val;
			return true;
		case vk_uint:
			val = vt.uint_val;
			return true;
		case vk_str:
			val = vt.str_val.size() != 0;
			return true;
		case vk_vector:
			val = vt.vector_val.size() != 0;
			return true;
		case vk_hash_map:
			val = vt.hash_map_val && vt.hash_map_val->size() != 0;
			return true;
		case vk_float:
		case vk_double:
			log(log_warning, "cannot convert floating point number (single or double precision) to boolean");
			return false;
		case vk_reference:
			if (vt.reference_val) {
				return convert_variant(*vt.reference_val, val);
			} else {
				return false;
			}
		case vk_buffer:
			if (vt.buffer_ptr_val) {
				return true;
			} else {
				return false;
			}
		}
	}

	variant *variant::raw_reference() {
		if (kind == vk_reference && reference_val) {
			return &*reference_val;
		}

		return nullptr;
	}

	const variant &variant::dereference() const {
		if (kind == vk_reference) {
			if (reference_val) {
				return *reference_val;
			} else {
				return vt_null;
			}
		} else {
			return *this;
		}
	}
	long int variant::as_syscall_type() const {
		switch (kind) {
		case vk_char:
			return char_val;
		case vk_null:
			return 0;
		case vk_bool:
			return bool_val ? 1 : 0;
		case vk_int:
			return int_val;
		case vk_uint:
			return uint_val;
		case vk_str:
			return (long int)str_val.c_str();
		case vk_vector:
			return (long int)&vector_val[0];
		case vk_hash_map:
			panic("no mapping between hash_maps and syscalls exists");
			return 0;
		case vk_float:
		case vk_double:
			panic("no mapping between floats and syscalls exists");
			return 0;
		case vk_reference:
			if (reference_val) {
				return reference_val->as_syscall_type();
			} else {
				panic("no referenced value exists in reference variable when used in syscall");
				return 0;
			}
		case vk_buffer:
			return (long int)buffer_ptr_val;
		}
	}
}
