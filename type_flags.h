#pragma once
#include "status.h"

struct type_flags_t {
	bool is_function;
	bool is_void;
	bool is_obj;
	bool is_struct;
	bool is_pointer;
};

type_flags_t infer_type_flags(
		status_t &status,
		const types::term::map &env,
		types::term::ref term,
		ptr<const ast::item> node);
