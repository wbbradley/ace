#pragma once
#include <memory>
#include <unistd.h>
#include "dbg.h"
#include "logger_decls.h"

#define dyncast std::dynamic_pointer_cast

template <typename T, typename U>
std::shared_ptr<T> safe_dyncast(U p) {
	auto up = dyncast<T>(p);
	if (p != nullptr && up == nullptr) {
		log_location(log_panic, p->get_location(), "couldn't upcast %s to a bound_var_t!", p->str().c_str());
		dbg();
	}
	return up;
}
