/*
 *  variant.h
 *
 *  Created by Will Bradley on 2/27/11.
 *  Copyright 2011, 2015, 2016 Will Bradley.
 *
 */
#pragma once
#include "variant_decls.h"
#include <string>
#include <ios>
#include <ostream>
#include <unordered_map>
#include <stdint.h>
#include <vector>
#include "ptr.h"

namespace runtime {
	const char *vk_to_str(variant_kind vk);
	bool vk_from_str(const std::string &kind, variant_kind &vk);

	struct variant {
		/* prevent accidental construction from the wrong things */
		template <typename T>
		variant(T t) = delete;

		variant() : kind(vk_null) {}
		variant(char val);
		variant(const char *val);
		variant(const std::string &val);
		variant(int val);
		variant(unsigned int val);
		variant(int64_t val);
		variant(uint64_t val);
		variant(double val);
		variant(float val);
		variant(bool val);
		variant(const ptr<variant> &var_ref);
		variant(const std::vector<variant> &vector);
		variant(const ptr<std::unordered_map<std::string, variant>> &hash_map);
		variant(char *buffer_ptr, size_t size);

		void write_as_json(std::ostream &os) const;
		std::string str(bool resolve_references=false) const;
		variant_kind kind;
		long int as_syscall_type() const;

		bool is_null() const { return kind == vk_null; }
		variant *raw_reference();
		const variant &dereference() const;

		std::string str_val;
		union {
			char char_val;
			int64_t int_val;
			uint64_t uint_val;
			float float_val;
			double double_val;
			bool bool_val;
		};
		ptr<variant> reference_val;
		std::vector<variant> vector_val;
		ptr<std::unordered_map<std::string, variant>> hash_map_val;
		char *buffer_ptr_val = nullptr;
		size_t buffer_size_val;

		bool operator ==(const runtime::variant &rhs) const;
		bool operator !=(const runtime::variant &rhs) const;
		bool operator <(const runtime::variant &rhs) const;
		bool operator <=(const runtime::variant &rhs) const;
		bool operator >(const runtime::variant &rhs) const;
		bool operator >=(const runtime::variant &rhs) const;
	};

	bool convert_variant(const runtime::variant &vt, runtime::variant &val);
	bool convert_variant(const runtime::variant &vt, std::string &str);
	bool convert_variant(const runtime::variant &vt, uint32_t &val);
	bool convert_variant(const runtime::variant &vt, int32_t &val);
	bool convert_variant(const runtime::variant &vt, uint64_t &val);
	bool convert_variant(const runtime::variant &vt, int64_t &val);
	bool convert_variant(const runtime::variant &vt, uint8_t &val);
	bool convert_variant(const runtime::variant &vt, int8_t &val);
	bool convert_variant(const runtime::variant &vt, float &val);
	bool convert_variant(const runtime::variant &vt, double &val);
	bool convert_variant(const runtime::variant &vt, bool &val);
	bool convert_variant(const runtime::variant &vt, ptr<variant> &val);
}

extern const runtime::variant vt_null;
