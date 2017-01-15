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
		rs.ss << tkstr(tk_matches) << " " << type->str();
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
		/* BLOCK */ {
			indented(rs);
			for (auto pattern_block : pattern_blocks) {
				pattern_block->render(rs);
			}
		}

		if (else_block != nullptr) {
			newline(rs);
			indent(rs);
			rs.ss << C_CONTROL << tkstr(tk_else) << C_RESET;
			newline(rs);
			indented(rs);
			else_block->render(rs);
		}
	}

	void pattern_block::render(render_state_t &rs) const {
		newline(rs);
		indent(rs);
		rs.ss << C_TYPE << tkstr(tk_is) << C_RESET << " ";
		rs.ss << type->str();
		newline(rs);
		indented(rs);
		block->render(rs);
	}

	void literal_expr::render(render_state_t &rs) const {
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
		rs.ss << C_TYPE << tkstr(tk_get_typeid) << C_RESET << "(";
		expr->render(rs);
		rs.ss << ")";
	}

	void sizeof_expr::render(render_state_t &rs) const {
		rs.ss << C_TYPE << tkstr(tk_sizeof) << C_RESET << "(";
		rs.ss << type->str();
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
		rs.ss << type->str();
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
		if (function_name.text != extern_function->token.text) {
			rs.ss << " " << C_SCOPE_SEP << tkstr(tk_to) << C_RESET;
			rs.ss << " " << function_name.text;
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

	void tag::render(render_state_t &rs) const {
		rs.ss << C_TYPE << tkstr(tk_tag) << C_RESET << " ";
		rs.ss << C_ID << token.text << C_RESET;
		newline(rs);
	}

	void type_def::render(render_state_t &rs) const {
		rs.ss << tkstr(tk_type) << " ";
		type_decl->render(rs);
		rs.ss << " ";
		type_algebra->render(rs);
	}

	void type_sum::render(render_state_t &rs) const {
		rs.ss << tkstr(tk_is);
		rs.ss << " " << type->str();
	}

	void var_decl::render(render_state_t &rs) const {
		if (rs.param_list_decl_depth == 0) {
			rs.ss << C_TYPE << tkstr(tk_var) << C_RESET << " ";
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
		if (inbound_context != nullptr) {
			rs.ss << '[' << inbound_context->str() << ']';
			newline(rs);
			indent(rs);
		}
		rs.ss << C_TYPE << tkstr(tk_def) << C_RESET << " " << token.text;
		param_list_decl->render(rs);
		if (return_type != nullptr) {
			rs.ss << " " << return_type->str();
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

    void bang_expr::render(render_state_t &rs) const {
        lhs->render(rs);
        rs.ss << "!";
    }

	void cast_expr::render(render_state_t &rs) const {
		lhs->render(rs);
		rs.ss << " as";
		if (force_cast) {
			rs.ss << "! ";
		} else {
			rs.ss << " ";
		}
		rs.ss << type_cast->str();
	}
}
