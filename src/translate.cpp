#include "translate.h"

using namespace bitter;

expr_t *translate(expr_t *expr, types::type_t::ref type, const env_t &env) {
	log("monomorphizing %s to have type %s", expr->str().c_str(), type->str().c_str());
	return expr;
}
