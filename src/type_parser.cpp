#include "zion.h"
#include "parser.h"
#include "parse_state.h"
#include "type_parser.h"

bool token_is_illegal_in_type(const token_t &token) {
	if (token.tk == tk_outdent) {
		return false;
	}
	return token.tk == tk_identifier && (
			token.text == K(to) ||
			token.text == K(def) ||
			token.text == K(where) ||
			token.text == K(any) ||
			token.text == K(link) ||
			token.text == K(link) ||
			token.text == K(struct) ||
			token.text == K(has) ||
			token.text == K(is) ||
			token.text == K(or) ||
			token.text == K(and) ||
			token.text == K(any));
}

namespace types {
	type_t::ref parse_and_type(parse_state_t &ps, const identifier::set &generics);

	type_t::ref parse_product_type(parse_state_t &ps, const identifier::set &generics) {
		assert(ps.token.is_ident(K(has)) || ps.token.is_ident(K(struct)));
		bool native_struct = ps.token.is_ident(K(struct));
		ps.advance();

		if (ps.token.tk != tk_indent && native_struct) {
			/* special case of empty structure */
			return ::type_struct({}, {});
		}

		chomp_token(tk_indent);
		type_t::refs dimensions;
		name_index_t name_index;
		int index = 0;
		while (ps.token.tk != tk_outdent) {
			if (!ps.line_broke() && ps.prior_token.tk != tk_indent) {
				throw user_error(ps.token.location, "product type dimensions must be separated by a newline");
			}

			token_t var_token;
			bool _mutable = ps.token.is_ident(K(var));
			if (ps.token.is_ident(K(var)) || ps.token.is_ident(K(let))) {
				ps.advance();
			}

			expect_token(tk_identifier);
			var_token = ps.token;
			if (name_index.find(var_token.text) != name_index.end()) {
				throw user_error(ps.token.location, "name " c_id("%s") " already exists in type", var_token.text.c_str());
			}
			name_index[var_token.text] = index++;
			ps.advance();

			type_t::ref dim_type = parse_type(ps, generics);
			if (_mutable) {
				dim_type = type_ref(dim_type);
			}

			dimensions.push_back(dim_type);
		}
		chomp_token(tk_outdent);
		return ::type_struct(dimensions, name_index);
	}

	type_t::ref parse_identifier_type(parse_state_t &ps, const identifier::set &generics) {
		expect_token(tk_identifier);
		type_t::ref cur_type;
		std::list<identifier::ref> ids;
		location_t location = ps.token.location;
		while (ps.token.tk == tk_identifier) {
			ids.push_back(make_code_id(ps.token));
			ps.advance();
			if (ps.token.tk == SCOPE_TK) {
				ps.advance();
				expect_token(tk_identifier);
			} else {
				break;
			}
		}

		/* reduce the type-path to a single simplified id */
		identifier::ref id = reduce_ids(ids, location);

		debug_above(9, log("checking what " c_id("%s") " is",
					id->str().c_str()));

		/* stash the identifier */
		if (generics.find(id) != generics.end()) {
			/* this type is marked as definitely unbound - aka generic. let's
			 * create a generic for it */
			return type_variable(id);
		} else {
			/* this is not a generic */
			if (in(id->get_name(), ps.type_macros)) {
				debug_above(9, log("checking whether type " c_id("%s") " expands...",
							id->get_name().c_str()));

				/* macro type expansion */
				return ps.type_macros[id->get_name()];
			} else if (id->get_name().find(SCOPE_SEP_CHAR) != std::string::npos) {
				/* if we're explicit about the type path, then let's just
				 * use that as the id */
				return type_id(id);
			} else {
				/* we don't have a macro/type_name link for this type, so
				 * let's assume it's in this module */
				if (ps.module_id->get_name() == GLOBAL_SCOPE_NAME) {
					/* the std module is the only "global" module */
					return type_id(id);
				} else {
					assert(ps.module_id->get_name().size() != 0);
					return type_id(reduce_ids({ps.module_id, id}, location));
				}
			}
		}
	}

	type_t::ref parse_parens_type(parse_state_t &ps, const identifier::set &generics) {
		chomp_token(tk_lparen);
		auto lhs = parse_type(ps, generics);
		if (ps.token.tk == tk_comma) {
			/* we've got a tuple expression */
			std::vector<type_t::ref> terms;
			terms.push_back(lhs);
			while (ps.token.tk == tk_comma) {
				ps.advance();
				if (ps.token.tk == tk_rcurly) {
					/* allow for ending with a comma */
					break;
				}
				auto next_term = parse_type(ps, generics);
				terms.push_back(next_term);
			}

			chomp_token(tk_rparen);
			return type_tuple(terms);
		} else {
			/* we've got a single expression */
			chomp_token(tk_rparen);
			return lhs;
		}
	}

	type_t::ref parse_type_constraints(parse_state_t &ps, const identifier::set &generics) {
		expect_ident(K(where));
		location_t where_location = ps.token.location;
		ps.advance();

		return parse_type(ps, generics);
	}

	types::type_args_t::ref parse_type_args(parse_state_t &ps, const identifier::set &generics) {
		chomp_token(tk_lparen);
		types::type_t::refs param_types;
		identifier::refs param_names;

		while (true) {
			if (ps.token.tk == tk_identifier) {
				auto var_name = ps.token;
				ps.advance();

				/* parse the type */
				if (ps.token.tk == tk_comma || ps.token.tk == tk_rparen) {
					/* if there is no type then assume `any` */
					param_types.push_back(type_variable(var_name.location));
				} else {
					param_types.push_back(parse_type(ps, generics));
				}

				auto param_name = make_code_id(var_name);
				if (in_vector(param_name, param_names)) {
					throw user_error(ps.token.location, "duplicated parameter name: %s", var_name.text.c_str());
				} else {
					param_names.push_back(param_name);
				}

				if (ps.token.tk == tk_rparen) {
					ps.advance();
					break;
				}
				if (ps.token.tk == tk_comma) {
					/* advance past a comma */
					ps.advance();
				}
			} else if (ps.token.tk == tk_rparen) {
				ps.advance();
				break;
			} else {
				throw user_error(ps.token.location, "expected a parameter name");
			}
		}
		return type_args(param_types, param_names);
	}

	types::type_args_t::ref parse_data_ctor_type(
			parse_state_t &ps,
		   	const identifier::set &generics)
   	{
		types::type_args_t::ref type_args;
		if (ps.token.tk == tk_lparen) {
			return parse_type_args(ps, generics);
		} else {
			return ::type_args({}, {});
		}
	}

	types::type_t::ref parse_function_type(parse_state_t &ps, identifier::set generics, identifier::ref &name) {
		chomp_ident(K(def));
		if (ps.token.tk == tk_identifier) {
			name = make_code_id(ps.token);
			ps.advance();
		} else {
			name.reset();
		}

		types::type_t::ref type_constraints;
		if (ps.token.tk == tk_lsquare) {
			auto constraints_token = ps.token;
			ps.advance();
			while (ps.token.tk == tk_identifier) {
				auto ftv = make_code_id(ps.token);

				if (in(ftv, generics)) {
					auto iter = generics.find(ftv);
					auto error = user_error(ftv->get_location(),
							"illegal redeclaration of type variable %s", 
							ftv->str().c_str());
					error.add_info((*iter)->get_location(), "see original declaration of type variable %s",
							(*iter)->str().c_str());
					throw error;
				}

				generics.insert(ftv);
				ps.advance();

				if (ps.token.tk == tk_comma) {
					ps.advance();
					expect_token(tk_identifier);
					if (token_is_illegal_in_type(ps.token)) {
						throw user_error(ps.token.location, "invalid type variable name %s", ps.token.str().c_str());
						break;
					}
					continue;
				} else if (ps.token.is_ident(K(where))) {
					type_constraints = parse_type_constraints(ps, generics);
					chomp_token(tk_rsquare);
					break;
				} else if (ps.token.tk == tk_rsquare) {
					ps.advance();
					break;
				} else {
					throw user_error(ps.token.location, "expected ',', 'where', or '}'");
					break;
				}
			}
		}

		type_args_t::ref type_args = parse_type_args(ps, generics);

		types::type_t::ref return_type;
		/* now let's parse the return type */
		if (!ps.line_broke()) {
			return_type = parse_type(ps, generics);
		} else {
			return_type = type_void();
		}

		auto type = type_function(type_constraints, type_args, return_type);
		if (name != nullptr) {
			return type;
		} else {
			return type_function_closure(type);
		}
	}

	type_t::ref parse_map_type(parse_state_t &ps, const identifier::set &generics) {
		/* we've got a map type */
		auto curly_token = ps.token;
		ps.advance();
		auto lhs = parse_type(ps, generics);
		chomp_token(tk_colon);
		auto rhs = parse_type(ps, generics);
		chomp_token(tk_rcurly);
		return type_operator(
				type_operator(type_id(make_iid_impl(STD_MAP_TYPE, curly_token.location)), lhs),
				rhs);
	}

	type_t::ref parse_vector_type(parse_state_t &ps, const identifier::set &generics) {
		/* we've got a map type */
		auto square_token = ps.token;
		ps.advance();
		auto lhs = parse_type(ps, generics);
		chomp_token(tk_rsquare);
		return type_operator(type_id(make_iid_impl(STD_VECTOR_TYPE, square_token.location)), lhs);
	}

	type_t::ref parse_integer_type(parse_state_t &ps, const identifier::set &generics) {
		auto token = ps.token;
		chomp_ident(K(integer));
		if (ps.token.tk != tk_lparen) {
			throw user_error(ps.token.location, "native integer types use the form " c_type("integer(bit_size, signed) ")
					"where bit_size must evaluate (as a type literal) to a valid word size, and signed "
					"must evaluate (as a type literal) to true or false. for example: " c_type("integer(8, false)") " is an unsigned octet (aka: a byte)");
		}
		chomp_token(tk_lparen);
		auto bit_size = parse_type(ps, generics);
		chomp_token(tk_comma);
		auto signed_ = parse_type(ps, generics);
		chomp_token(tk_rparen);
		return type_integer(bit_size, signed_);
	}

	type_t::ref parse_lambda_type(parse_state_t &ps, const identifier::set &generics) {
		if (ps.token.is_ident(K(lambda))) {
			ps.advance();
			expect_token(tk_identifier);
			auto param_token = ps.token;
			ps.advance();
			auto body = parse_and_type(ps, generics);
			return type_lambda(make_code_id(param_token), body);
		} else if (ps.token.is_ident(K(def))) {
			identifier::ref name;
			auto fn_type = parse_function_type(ps, generics, name);
			if (name != nullptr && name->get_name() != "_") {
				auto error = user_error(name->get_location(), "function name unexpected in this context (" c_id("%s") ")",
						name->get_name().c_str());
				error.add_info(fn_type->get_location(), "while parsing type %s", fn_type->str().c_str());
				error.add_info(fn_type->get_location(), "note: to describe an unbound function type use the name '_'");
				throw error;
			}
			return fn_type;
		} else if (ps.token.is_ident(K(any))) {
			auto token = ps.token;
			ps.advance();

			type_t::ref type;
			if (ps.token.tk == tk_identifier && !token_is_illegal_in_type(ps.token)) {
				/* named generic */
				type = type_variable(make_code_id(ps.token));
				ps.advance();
			} else {
				/* no named generic */
				type = type_variable(token.location);
			}
			return type;
		} else if (ps.token.is_ident(K(integer))) {
			return parse_integer_type(ps, generics);
		} else if (ps.token.tk == tk_lparen) {
			return parse_parens_type(ps, generics);
		} else if (ps.token.tk == tk_lcurly) {
			return parse_map_type(ps, generics);
		} else if (ps.token.tk == tk_lsquare) {
			return parse_vector_type(ps, generics);
		} else if ((ps.token.tk == tk_integer) || (ps.token.tk == tk_string)) {
			auto type = type_literal(ps.token);
			ps.advance();
			return type;
		} else if (ps.token.tk == tk_identifier) {
			if (token_is_illegal_in_type(ps.token)) {
				/* this type is done */
				return nullptr;
			} else {
				return parse_identifier_type(ps, generics);
			}
		} else {
			return nullptr;
		}
	}

	type_t::ref parse_ptr_type(parse_state_t &ps, const identifier::set &generics, bool disallow_maybe=false) {
		bool is_ptr = false;
		bool is_maybe = false;
		if (ps.token.tk == tk_times) {
			is_ptr = true;
			ps.advance();
			if (ps.token.tk == tk_maybe) {
				is_maybe = true;
				ps.advance();
			}
		}
		/* if we had one pointer, we may have another. if we had no pointer, then we are done checking for pointers */
		auto element = is_ptr ? parse_ptr_type(ps, generics, is_ptr) : parse_lambda_type(ps, generics);
		if (element == nullptr) {
			/* there is nothing left for us to parse */
			return nullptr;
		}

		if (is_maybe) {
			if (ps.token.tk == tk_maybe) {
				throw user_error(ps.token.location, "redundant usage of ?. you may need parentheses");
			} else {
				return type_maybe(type_ptr(element), {});
			}
		} else if (is_ptr) {
			if (ps.token.tk == tk_maybe) {
				throw user_error(ps.token.location, "use *? for native pointers. or, you may need parentheses");
			} else {
				return type_ptr(element);
			}
		} else {
			if (ps.token.tk == tk_maybe) {
				if (disallow_maybe) {
					throw user_error(ps.token.location, "ambiguous ?. try using `*?`, or parentheses");
				} else {
					ps.advance();
					return type_maybe(element, {});
				}
			} else {
				return element;
			}
		}
	}

	type_t::ref parse_ref_type(parse_state_t &ps, const identifier::set &generics) {
		bool is_ref = false;
		if (ps.token.tk == tk_ampersand) {
			ps.advance();
			is_ref = true;
		}
		auto element = parse_ptr_type(ps, generics);
		return is_ref ? type_ref(element) : element;
	}

	type_t::ref parse_application_type(parse_state_t &ps, const identifier::set &generics) {
		auto lhs = parse_ref_type(ps, generics);
		if (lhs == nullptr) {
			throw user_error(ps.token.location, "unable to parse type");
		} else {
			std::vector<type_t::ref> terms;
			while (true) {
				if (ps.line_broke()) {
					break;
				}
				auto next_term = parse_ref_type(ps, generics);
				if (next_term != nullptr) {
					terms.push_back(next_term);
				} else {
					break;
				}
			}

			for (unsigned i = 0; i < terms.size(); ++i) {
				lhs = type_operator(lhs, terms[i]);
			}
			return lhs;
		}
	}

	type_t::ref parse_infix_subtype(parse_state_t &ps, const identifier::set &generics) {
		auto lhs = parse_application_type(ps, generics);
		if (ps.token.tk == tk_subtype) {
			/* we've got a subtype expression */
			ps.advance();
			auto rhs = parse_application_type(ps, generics);

			return type_subtype(lhs, rhs);
		} else {
			/* we've got a single expression */
			return lhs;
		}
	}

	type_t::ref parse_eq_type(parse_state_t &ps, const identifier::set &generics) {
		location_t location = ps.token.location;
		auto lhs = parse_infix_subtype(ps, generics);
		if (ps.token.tk == type_eq_t::TK) {
			location = ps.token.location;
			ps.advance();
			/* we've got an equality condition */
			type_t::ref rhs = parse_infix_subtype(ps, generics);
			return type_eq(lhs, rhs, location);
		} else {
			return lhs;
		}
	}

	type_t::ref parse_and_type(parse_state_t &ps, const identifier::set &generics) {
		auto lhs = parse_eq_type(ps, generics);
		if (ps.token.is_ident(K(and))) {
			/* we've got a Logical AND expression */
			std::vector<type_t::ref> terms;
			terms.push_back(lhs);
			while (ps.token.is_ident(K(and))) {
				chomp_ident(K(and));
				terms.push_back(parse_eq_type(ps, generics));
			}

			return type_and(terms);
		} else {
			/* we've got a single expression */
			return lhs;
		}
	}

	type_t::ref parse_or_type(parse_state_t &ps, const identifier::set &generics) {
		return parse_and_type(ps, generics);
	}

	type_t::ref parse_type(parse_state_t &ps, const identifier::set &generics) {
		return parse_or_type(ps, generics);
	}

	identifier::ref reduce_ids(const std::list<identifier::ref> &ids, location_t location) {
		assert(ids.size() != 0);
		return make_iid_impl(join(ids, SCOPE_SEP), location);
	}
}

types::type_t::ref parse_type_expr(
		std::string input,
	   	identifier::set generics,
	   	identifier::ref module_id)
{
	std::istringstream iss(input);
	zion_lexer_t lexer("", iss);
	type_macros_t global_type_macros;

	add_default_type_macros(global_type_macros);
	global_type_macros[MANAGED_STR] = type_id(make_iid(MANAGED_STR));
	global_type_macros[MANAGED_INT] = type_id(make_iid(MANAGED_INT));
	global_type_macros[MANAGED_FLOAT] = type_id(make_iid(MANAGED_FLOAT));

	parse_state_t ps("", lexer, global_type_macros, global_type_macros, nullptr);
	if (module_id != nullptr) {
		ps.module_id = module_id;
	} else {
		ps.module_id = make_iid("__parse_type_expr__");
	}
	debug_above(8, log("parsing %s", input.c_str()));
	return types::parse_type(ps, generics);
}

