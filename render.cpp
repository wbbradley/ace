#include "ast.h"

namespace ast {
   	struct render_state_t {
		bool syntax_highlighting = true;
		int indent = 0;
		int param_list_decl_depth = 0;
		std::stringstream ss;
	};

	std::string item::str() const {
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

	void assignment::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);
	}

	void break_flow::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << tkstr(tk_break) << C_RESET << " ";
	}

	void times_expr::render(render_state_t &rs) const {
		rs.ss << "(";

		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);

		rs.ss << ")";
	}

	void type_alias::render(render_state_t &rs) const {
		rs.ss << tkstr(tk_matches) << " ";
		type_ref->render(rs);
	}

	void prefix_expr::render(render_state_t &rs) const {
		rs.ss << token.text;
		if (isalpha(token.text[0])) {
			rs.ss << " ";
		}

		rhs->render(rs);
	}

	void while_block::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << tkstr(tk_while) << C_RESET << " ";
		condition->render(rs);
		newline(rs);

		indented(rs);
		block->render(rs);
	}

	void when_block::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << tkstr(tk_when) << C_RESET << " ";
		value->render(rs);
		newline(rs);
		indented(rs);
		for (auto pattern_block : pattern_blocks) {
			pattern_block->render(rs);
		}
		if (else_block != nullptr) {
			else_block->render(rs);
		}
	}

	void literal_expr::render(render_state_t &rs) const {
		rs.ss << token.text;
	}

	void type_product::render(render_state_t &rs) const {
		rs.ss << C_TYPE << tkstr(tk_has) << C_RESET;
		newline(rs);

		indented(rs);
		for (int i = 0; i < dimensions.size(); ++i) {
			if (i > 0) {
				newline(rs);
			}

			indent(rs);
			dimensions[i]->render(rs);
		}
	}

	void typeid_expr::render(render_state_t &rs) const {
		rs.ss << C_TYPE << tkstr(tk_typeid) << C_RESET << "(";
		expr->render(rs);
		rs.ss << ")";
	}

	void callsite_expr::render(render_state_t &rs) const {
		function_expr->render(rs);
		params->render(rs);
	}

	void continue_flow::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << tkstr(tk_continue) << C_RESET;
	}

	void function_defn::render(render_state_t &rs) const {
		newline(rs);
		decl->render(rs);
		newline(rs);

		indented(rs);
		block->render(rs);
	}

	void type_ref_named::render(render_state_t &rs) const {
		rs.ss << term->str();
	}

	void type_ref_list::render(render_state_t &rs) const {
		rs.ss << '[';
		type_ref->render(rs);
	   	rs.ss << ']';
	}

	void type_ref_tuple::render(render_state_t &rs) const {
		rs.ss << '(';
		for (int i = 0; i < type_refs.size(); ++i) {
			if (i > 0) {
				rs.ss << ", ";
			}
			type_refs[i]->render(rs);
		}
		rs.ss << ')';
	}

	void mod_assignment::render(render_state_t &rs) const {
		not_done();
	}

	void reference_expr::render(render_state_t &rs) const {
		rs.ss << C_VAR << token.text << C_RESET;
	}

	void dimension::render(render_state_t &rs) const {
		if (!!name) {
			rs.ss << C_TYPE << tkstr(tk_var) << C_RESET << " ";
			rs.ss << C_VAR << name << C_RESET << " ";
		}

		type_ref->render(rs);
	}

	void type_ref_generic::render(render_state_t &rs) const {
		rs.ss << term->str();
	}

	void plus_assignment::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);
	}

	void array_index_expr::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << "[";
		index->render(rs);
		rs.ss << "]";
	}

	void minus_assignment::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);
	}

	void times_assignment::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);
	}

	void divide_assignment::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);
	}

	void array_literal_expr::render(render_state_t &rs) const {
		rs.ss << "[";
		for (int i = 0; i < items.size(); ++i) {
			if (i > 0) {
				rs.ss << ", ";
			}

			items[i]->render(rs);
		}
		rs.ss << "]";
	}

	void link_module_statement::render(render_state_t &rs) const {
		rs.ss << C_SCOPE_SEP << tkstr(tk_link) << C_RESET;
		rs.ss << " ";
		extern_module->render(rs);
		if (link_as_name.text != extern_module->token.text) {
			rs.ss << " " << C_SCOPE_SEP << tkstr(tk_to) << C_RESET;
			rs.ss << " " << link_as_name.text;
		}
	}

	void link_function_statement::render(render_state_t &rs) const {
		rs.ss << C_SCOPE_SEP << tkstr(tk_link) << C_RESET;
		rs.ss << " ";
		extern_function->render(rs);
		if (link_as_name.text != extern_function->token.text) {
			rs.ss << " " << C_SCOPE_SEP << tkstr(tk_to) << C_RESET;
			rs.ss << " " << link_as_name.text;
		}
	}

	void block::render(render_state_t &rs) const {
		for (int i = 0; i < statements.size(); ++i) {
			if (i > 0) {
				newline(rs);
			}

			indent(rs);
			statements[i]->render(rs);
		}
	}

	void eq_expr::render(render_state_t &rs) const {
		rs.ss << "(";

		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);

		rs.ss << ")";
	}

	void tuple_expr::render(render_state_t &rs) const {
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

	void or_expr::render(render_state_t &rs) const {
		rs.ss << "(";

		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);

		rs.ss << ")";
	}

	void and_expr::render(render_state_t &rs) const {
		rs.ss << "(";

		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);

		rs.ss << ")";
	}

	void dot_expr::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << C_SCOPE_SEP << '.' << C_RESET;
		rs.ss << rhs.text;
	}

	void if_block::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << tkstr(tk_if) << C_RESET << " ";
		condition->render(rs);

		newline(rs);

		indented(rs);
		block->render(rs);
	}

	void type_def::render(render_state_t &rs) const {
		rs.ss << tkstr(tk_type) << " ";
		type_decl->render(rs);
		rs.ss << " ";
		type_algebra->render(rs);
	}

	void type_sum::render(render_state_t &rs) const {
		rs.ss << tkstr(tk_is);
		if (data_ctors.size() <= 1) {
			rs.ss << " ";
			data_ctors[0]->render(rs);
		} else {
			newline(rs);
			indented(rs);
			for (int i = 0; i < data_ctors.size(); ++i) {
				if (i > 0) {
					rs.ss << " " << tkstr(tk_or);
					newline(rs);
				}

				indent(rs);
				data_ctors[i]->render(rs);
			}
		}
	}

	void data_ctor::render(render_state_t &rs) const {
		rs.ss << token.text;
		if (type_ref_params.size() != 0) {
			rs.ss << "(";
			for (int i = 0; i < type_ref_params.size(); ++i) {
				if (i > 0) {
					rs.ss << ", ";
				}

				type_ref_params[i]->render(rs);
			}
			rs.ss << ")";
		}
	}

	void var_decl::render(render_state_t &rs) const {
		if (rs.param_list_decl_depth == 0) {
			rs.ss << C_TYPE << tkstr(tk_var) << C_RESET << " ";
		}

		rs.ss << C_VAR << token.text << C_RESET;

		if (type_ref != nullptr) {
			rs.ss << " ";
			type_ref->render(rs);
		}

		if (initializer != nullptr) {
			rs.ss << " = ";
			initializer->render(rs);
		}
	}

	void ineq_expr::render(render_state_t &rs) const {
		rs.ss << "(";

		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);

		rs.ss << ")";
	}

	void pass_flow::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << tkstr(tk_pass) << C_RESET;
	}

	void plus_expr::render(render_state_t &rs) const {
		rs.ss << "(";

		lhs->render(rs);
		rs.ss << " " << token.text << " ";
		rhs->render(rs);

		rs.ss << ")";
	}

	void type_decl::render(render_state_t &rs) const {
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

	void param_list::render(render_state_t &rs) const {
		rs.ss << "(";

		for (int i = 0; i < expressions.size(); ++i) {
			if (i > 0) {
				rs.ss << ", ";
			}

			expressions[i]->render(rs);
		}

		rs.ss << ")";
	}

	void module_decl::render(render_state_t &rs) const {
		rs.ss << C_MODULE << tkstr(tk_module) << C_RESET << " " << get_canonical_name();
		if (semver != nullptr) {
			rs.ss << " ";
			semver->render(rs);
		}
	}

	void function_decl::render(render_state_t &rs) const {
		rs.ss << C_TYPE << tkstr(tk_def) << C_RESET << " " << token.text;
		param_list_decl->render(rs);
		if (return_type_ref != nullptr) {
			rs.ss << " ";
			return_type_ref->render(rs);
		}
	}

	void param_list_decl::render(render_state_t &rs) const {
		++rs.param_list_decl_depth;

		rs.ss << "(";

		for (int i = 0; i < params.size(); ++i) {
			if (i > 0) {
				rs.ss << ", ";
			}
			params[i]->render(rs);
		}
		rs.ss << ")";

		--rs.param_list_decl_depth;
	}

	void return_statement::render(render_state_t &rs) const {
		rs.ss << C_CONTROL << tkstr(tk_return) << C_RESET << " ";
		if (expr != nullptr) {
			expr->render(rs);
		}
	}

	void module::render(render_state_t &rs) const {
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

	void semver::render(render_state_t &rs) const {
		rs.ss << token.text;
	}

	void program::render(render_state_t &rs) const {
		int count = 0;
		for (auto &module : modules) {
			if (count++ > 0) {
				newline(rs, 2);
			}

			module->render(rs);
		}
	}
}
