#include "types.h"

namespace types {
	struct type_id;
	struct type_variable;
	struct type_operator;
	struct type_product;

	struct type_visitor {
		virtual bool visit(const type_id &id) = 0;
		virtual bool visit(const type_variable &variable) = 0;
		virtual bool visit(const type_operator &operator_) = 0;
		virtual bool visit(const type_product &product) = 0;
		virtual bool visit(const type_sum &sum) = 0;
	};
}

