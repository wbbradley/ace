#include "ast.h"

namespace ast {
   	struct render_state_t {
		bool syntax_highlighting = true;
		int indent = 0;
		int param_list_decl_depth = 0;
		std::stringstream ss;
	};

	std::string item_t::str() const {
		render_state_t render_state;
		this->render(render_state);
		return render_state.ss.str();
	}

	struct indented_t {
		indented_t(render_state_t &rs) : rs(rs) { ++rs.indent; }
		~indented_t() { --rs.indent; }

		render_state_t &rs;
	};

	#define not_done() log(log_info, "so far:\n%s", rs.ss.str().c_str()); not_impl()
	#define indented(rs) indented_t indented_(rs)

	void indent(render_state_t &rs) {
		if (rs.indent > 0) {
			assert(rs.indent < 10000);
			rs.ss << std::string(size_t(rs.indent), '\t');
		}
	}

	void newline(render_state_t &rs, int count=1) {
		for (int i = 0; i < count; ++i) {
			rs.ss << std::endl;
		}
	}

	void assignment_t::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);
	}

	void break_flow_t::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << K(break) << C_RESET << " ";
	}

	void binary_operator_t::render(render_state_t &rs) const {
		rs.ss << "(";

		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);

		rs.ss << ")";
	}

	void type_alias_t::render(render_state_t &rs) const {
		rs.ss << "= " << type->str();
	}

	void prefix_expr_t::render(render_state_t &rs) const {
		rs.ss << "(";
		rs.ss << token.text;
		if (isalpha(token.text[0])) {
			rs.ss << " ";
		}
		rhs->render(rs);
		rs.ss << ")";
	}

	void while_block_t::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << K(while) << C_RESET << " ";
		condition->render(rs);
		newline(rs);

		indented(rs);
		block->render(rs);
	}

	void for_block_t::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << K(for) << C_RESET;
		rs.ss << " " << C_ID << var_token.text << C_RESET;
		rs.ss << " " << C_CONTROL << K(in) << C_RESET << " ";
		iterable->render(rs);
		newline(rs);

		indented(rs);
		block->render(rs);
	}

	void when_block_t::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << K(when) << C_RESET << " ";
		value->render(rs);
		/* BLOCK */ {
			indented(rs);
			for (auto pattern_block : pattern_blocks) {
				pattern_block->render(rs);
			}
		}

		if (else_block != nullptr) {
			newline(rs);
			indent(rs);
			rs.ss << C_CONTROL << K(else) << C_RESET;
			newline(rs);
			indented(rs);
			else_block->render(rs);
		}
	}

	void pattern_block_t::render(render_state_t &rs) const {
		newline(rs);
		indent(rs);
		rs.ss << C_TYPE << K(is) << C_RESET << " ";
		rs.ss << type->str();
		newline(rs);
		indented(rs);
		block->render(rs);
	}

	void literal_expr_t::render(render_state_t &rs) const {
		switch (token.tk) {
		case tk_string:
			rs.ss << C_ERROR << token.text << C_RESET;
			break;
		case tk_integer:
		case tk_float:
			rs.ss << C_CONTROL << token.text << C_RESET;
			break;
		default:
			rs.ss << C_LINE_REF << token.text << C_RESET;
			break;
		}
	}

	void type_product_t::render(render_state_t &rs) const {
		rs.ss << type->str();
	}

	void type_link_t::render(render_state_t &rs) const {
		rs.ss << K(link) << " ";
		rs.ss << underlying_type->str() << " ";
		rs.ss << finalize_fn.text << " ";
		rs.ss << mark_fn.text;
		newline(rs);
	}

	void typeid_expr_t::render(render_state_t &rs) const {
		rs.ss << C_TYPE << K(__get_typeid__) << C_RESET << "(";
		expr->render(rs);
		rs.ss << ")";
	}

	void sizeof_expr_t::render(render_state_t &rs) const {
		rs.ss << C_TYPE << K(sizeof) << C_RESET << "(";
		rs.ss << type->str();
		rs.ss << ")";
	}

	void callsite_expr_t::render(render_state_t &rs) const {
		function_expr->render(rs);
		rs.ss << "(";

		for (size_t i = 0; i < params.size(); ++i) {
			if (i > 0) {
				rs.ss << ", ";
			}

			params[i]->render(rs);
		}

		rs.ss << ")";
	}

	void typeinfo_expr_t::render(render_state_t &rs) const {
		rs.ss << C_TYPE << "typeinfo" << C_RESET;
		rs.ss << "(" << type->str() << ")";
	}

	void continue_flow_t::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << K(continue) << C_RESET;
	}

	void function_defn_t::render(render_state_t &rs) const {
		newline(rs);
		decl->render(rs);
		newline(rs);

		indented(rs);
		block->render(rs);
	}

	void mod_assignment_t::render(render_state_t &rs) const {
		not_done();
	}

	void reference_expr_t::render(render_state_t &rs) const {
		rs.ss << C_VAR << token.text << C_RESET;
	}

	void dimension_t::render(render_state_t &rs) const {
		if (name.size() != 0) {
			rs.ss << C_TYPE << K(var) << C_RESET << " ";
			rs.ss << C_VAR << name << C_RESET << " ";
		}
		rs.ss << type->str();
	}

	void plus_assignment_t::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);
	}

	void array_index_expr_t::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << "[";
		index->render(rs);
		rs.ss << "]";
	}

	void minus_assignment_t::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);
	}

	void times_assignment_t::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);
	}

	void divide_assignment_t::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);
	}

	void array_literal_expr_t::render(render_state_t &rs) const {
		rs.ss << "[";
		for (size_t i = 0; i < items.size(); ++i) {
			if (i > 0) {
				rs.ss << ", ";
			}

			items[i]->render(rs);
		}
		rs.ss << "]";
	}

	void link_module_statement_t::render(render_state_t &rs) const {
		rs.ss << C_SCOPE_SEP << K(link) << C_RESET;
		rs.ss << " ";
		extern_module->render(rs);
		if (link_as_name.text != extern_module->token.text) {
			rs.ss << " " << C_SCOPE_SEP << K(as) << C_RESET;
			rs.ss << " " << link_as_name.text;
		}
	}

	void link_name_t::render(render_state_t &rs) const {
		rs.ss << C_SCOPE_SEP << K(link) << C_RESET;
		rs.ss << " " << local_name.text << " " << K(to) << " ";
		extern_module->render(rs);
		rs.ss << "." << remote_name.text;
	}

	void link_function_statement_t::render(render_state_t &rs) const {
		rs.ss << C_SCOPE_SEP << K(link) << C_RESET;
		rs.ss << " ";
		extern_function->render(rs);
		if (extern_function->link_to_name.text != extern_function->token.text) {
			rs.ss << " " << C_SCOPE_SEP << K(to) << C_RESET;
			rs.ss << " " << extern_function->link_to_name.text;
		}
	}

	void block_t::render(render_state_t &rs) const {
		for (size_t i = 0; i < statements.size(); ++i) {
			if (i > 0) {
				newline(rs);
			}

			indent(rs);
			statements[i]->render(rs);
		}
	}

	void tuple_expr_t::render(render_state_t &rs) const {
		rs.ss << "(";

		const char *sep = "";
		for (auto &value : values) {
			rs.ss << sep;
			value->render(rs);
			rs.ss << ",";
			sep = " ";
		}

		rs.ss << ")";
	}

	void or_expr_t::render(render_state_t &rs) const {
		rs.ss << "(";

		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);

		rs.ss << ")";
	}

	void ternary_expr_t::render(render_state_t &rs) const {
		condition->render(rs);
		rs.ss << " ? ";
		when_true->render(rs);
		rs.ss << " : ";
		when_false->render(rs);
	}

	void and_expr_t::render(render_state_t &rs) const {
		rs.ss << "(";

		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);

		rs.ss << ")";
	}

	void dot_expr_t::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << C_SCOPE_SEP << '.' << C_RESET;
		rs.ss << rhs.text;
	}

	void if_block_t::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << K(if) << C_RESET << " ";
		condition->render(rs);

		newline(rs);

		indented(rs);
		block->render(rs);
	}

	void tag_t::render(render_state_t &rs) const {
		rs.ss << C_TYPE << K(tag) << C_RESET << " ";
		rs.ss << C_ID << token.text << C_RESET;
		if (type_variables.size() != 0) {
			rs.ss << '{' << join(type_variables, ", ") << '}';
		}
		newline(rs);
	}

	void type_def_t::render(render_state_t &rs) const {
		rs.ss << K(type) << " ";
		type_decl->render(rs);
		rs.ss << " ";
		type_algebra->render(rs);
	}

	void type_sum_t::render(render_state_t &rs) const {
		rs.ss << K(is);
		rs.ss << " " << type->str();
	}

	void link_var_statement_t::render(render_state_t &rs) const {
		rs.ss << C_SCOPE_SEP << K(link) << C_RESET;
		rs.ss << " ";
		var_decl->render(rs);
	}

	void var_decl_t::render(render_state_t &rs) const {
		if (rs.param_list_decl_depth == 0) {
			rs.ss << C_TYPE << (is_let() ? K(let) : K(var)) << C_RESET << " ";
		}

		rs.ss << C_VAR << token.text << C_RESET;

		if (type != nullptr) {
			rs.ss << " " << type->str();
		}

		if (initializer != nullptr) {
			rs.ss << " = ";
			initializer->render(rs);
		}
	}

	void pass_flow_t::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << K(pass) << C_RESET;
	}

	void type_decl_t::render(render_state_t &rs) const {
		rs.ss << token.text;
		if (type_variables.size() != 0) {
			const char *sep = "";
			rs.ss << "{";
			for (auto var : type_variables) {
				rs.ss << sep << var;
				sep = ", ";
			}
			rs.ss << "}";
		}
	}

	void module_decl_t::render(render_state_t &rs) const {
		if (global) {
			rs.ss << C_MODULE << K(global) << C_RESET;
		} else {
			rs.ss << C_MODULE << K(module) << C_RESET << " " << get_canonical_name();
			if (semver != nullptr) {
				rs.ss << " ";
				semver->render(rs);
			}
		}
	}

	void function_decl_t::render(render_state_t &rs) const {
		if (extends_module != nullptr) {
			rs.ss << '[' << K(module) << " " << C_MODULE << extends_module->get_name() << C_RESET << ']';
			newline(rs);
			indent(rs);
		}
		rs.ss << C_TYPE << K(def) << C_RESET << " " << token.text;
		param_list_decl->render(rs);
		if (return_type != nullptr) {
			rs.ss << " " << return_type->str();
		}
	}

	void param_list_decl_t::render(render_state_t &rs) const {
		++rs.param_list_decl_depth;

		rs.ss << "(";

		for (size_t i = 0; i < params.size(); ++i) {
			if (i > 0) {
				rs.ss << ", ";
			}
			params[i]->render(rs);
		}
		rs.ss << ")";

		--rs.param_list_decl_depth;
	}

	void return_statement_t::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << K(return) << C_RESET << " ";
		if (expr != nullptr) {
			expr->render(rs);
		}
	}

	void module_t::render(render_state_t &rs) const {
		decl->render(rs);
		newline(rs, 2);

		for (auto &linked_module : linked_modules) {
			linked_module->render(rs);
			newline(rs, 2);
		}

		for (auto &linked_function : linked_functions) {
			linked_function->render(rs);
			newline(rs, 2);
		}

		for (auto &type_def : type_defs) {
			type_def->render(rs);
			newline(rs, 2);
		}

		for (auto &function : functions) {
			function->render(rs);
			newline(rs, 2);
		}
	}

	void semver_t::render(render_state_t &rs) const {
		rs.ss << token.text;
	}

	void program_t::render(render_state_t &rs) const {
		int count = 0;
		for (auto &module : modules) {
			if (count++ > 0) {
				newline(rs, 2);
			}

			module->render(rs);
		}
	}

    void bang_expr_t::render(render_state_t &rs) const {
        lhs->render(rs);
        rs.ss << "!";
    }

	void cast_expr_t::render(render_state_t &rs) const {
		rs.ss << "((";
		lhs->render(rs);
		rs.ss << ") as";
		if (force_cast) {
			rs.ss << "! ";
		} else {
			rs.ss << " ";
		}
		rs.ss << type_cast->str() << ")";
	}
}
