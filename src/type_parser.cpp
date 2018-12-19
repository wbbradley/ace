#include "zion.h"
#include "parser.h"
#include "parse_state.h"
#include "type_parser.h"

// REVIEW: this function has a horrible and confusing name
bool token_is_illegal_in_type(const token_t &token) {
	if (token.tk == tk_lcurly || token.tk == tk_rcurly || token.tk == tk_expr_block) {
		return true;
	}
	return token.tk == tk_identifier && (
			token.text == K(to) ||
			token.text == K(fn) ||
			token.text == K(where) ||
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
	type_t::ref parse_and_type(parse_state_t &ps, const std::set<identifier_t> &generics);

	type_t::ref parse_product_type(parse_state_t &ps, const std::set<identifier_t> &generics) {
		assert(ps.token.is_ident(K(has)) || ps.token.is_ident(K(struct)));
		bool native_struct = ps.token.is_ident(K(struct));
		ps.advance();

		if (ps.token.tk != tk_lcurly && native_struct) {
			/* special case of empty structure */
			return ::type_struct({}, {});
		}

		chomp_token(tk_lcurly);
		type_t::refs dimensions;
		name_index_t name_index;
		int index = 0;
		while (ps.token.tk != tk_rcurly) {
			if (!ps.line_broke() && ps.prior_token.tk != tk_lcurly) {
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
		chomp_token(tk_rcurly);
		return ::type_struct(dimensions, name_index);
	}

	type_t::ref parse_identifier_type(parse_state_t &ps, const std::set<identifier_t> &generics) {
		expect_token(tk_identifier);
		type_t::ref cur_type;
		std::list<identifier_t> ids;
		location_t location = ps.token.location;
		while (ps.token.tk == tk_identifier) {
			ids.push_back(iid(ps.token));
			ps.advance();
			if (ps.token.tk == SCOPE_TK) {
				ps.advance();
				expect_token(tk_identifier);
			} else {
				break;
			}
		}

		/* reduce the type-path to a single simplified id */
		identifier_t id = reduce_ids(ids, location);

		debug_above(9, log("checking what " c_id("%s") " is", id.str().c_str()));

		/* stash the identifier */
		if (generics.find(id) != generics.end()) {
			/* this type is marked as definitely unbound - aka generic. let's
			 * create a generic for it */
			return type_variable(id);
		} else {
			/* this is not a generic */
			if (id.name.find(SCOPE_SEP_CHAR) != std::string::npos) {
				/* if we're explicit about the type path, then let's just
				 * use that as the id */
				return type_id(id);
			} else {
				/* we don't have a macro/type_name link for this type, so
				 * let's assume it's in this module */
				if (ps.module_name == GLOBAL_SCOPE_NAME) {
					/* the std module is the only "global" module */
					return type_id(id);
				} else {
					assert(ps.module_name.size() != 0);
					return type_id(reduce_ids({identifier_t{ps.module_name, INTERNAL_LOC()}, id}, location));
				}
			}
		}
	}

	type_t::ref parse_parens_type(parse_state_t &ps, const std::set<identifier_t> &generics) {
		chomp_token(tk_lparen);
        if (ps.token.tk == tk_rparen) {
            ps.advance();
            return type_unit();
        }
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

	type_t::ref parse_type_constraints(parse_state_t &ps, const std::set<identifier_t> &generics) {
		expect_ident(K(where));
		location_t where_location = ps.token.location;
		ps.advance();

		return parse_type(ps, generics);
	}

	types::type_t::ref parse_type_args(parse_state_t &ps, const std::set<identifier_t> &generics, bool automatic_any) {
		chomp_token(tk_lparen);
		if (ps.token.tk == tk_double_dot) {
			ps.advance();
			expect_token(tk_identifier);
			auto type_args = type_variable(iid(ps.token));
			ps.advance();
			chomp_token(tk_rparen);
			return type_args;
		}

		types::type_t::refs param_types;
		identifiers_t param_names;

		while (true) {
			if (ps.token.tk == tk_identifier) {
				auto var_name = ps.token;
				ps.advance();

				if (var_name.text == "_") {
					var_name.text = types::gensym(INTERNAL_LOC()).name;
				}

				/* parse the type */
				if (ps.token.tk == tk_comma || ps.token.tk == tk_rparen) {
					if (automatic_any) {
						/* if there is no type then assume `any` */
						param_types.push_back(type_variable(var_name.location));
					} else {
						throw user_error(var_name.location, "parameter is missing a type specifier");
					}
				} else {
					param_types.push_back(parse_type(ps, generics));
				}

				auto param_name = iid(var_name);

				/* check for duplicate param names */
				for (auto p : param_names) {
					if (([](identifier_t x) { return x.name; })(p) == param_name.name) {
						throw user_error(ps.token.location, "duplicated parameter name: %s", var_name.text.c_str());
					}
				}

				param_names.push_back(param_name);

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
		   	const std::set<identifier_t> &generics)
   	{
		types::type_args_t::ref type_args;
		if (ps.token.tk == tk_lparen) {
			auto type = parse_type_args(ps, generics, false /*automatic_any*/);
			type_args = dyncast<const types::type_args_t>(type);
			if (type_args == nullptr) {
				auto error = user_error(type->get_location(), "data ctors must contain non-generic type args");
				error.add_info(type->get_location(), "type of args is %s", type->str().c_str());
				throw error;
			}
			return type_args;
		} else {
			return ::type_args({}, {});
		}
	}

    types::type_t::ref parse_function_type(
            parse_state_t &ps,
			location_t location,
            std::set<identifier_t> generics,
            std::shared_ptr<identifier_t> &name,
            types::type_t::ref default_return_type)
    {
        if (ps.token.tk == tk_identifier) {
            name = std::make_shared<identifier_t>(identifier_t::from_token(ps.token));
            ps.advance();
        } else {
            name.reset();
        }

        if (default_return_type == nullptr) {
            // default_return_type = name != nullptr ? type_unit() : type_variable(ps.token.location);
            default_return_type = type_variable(ps.token.location);
        }

        types::type_t::ref type_constraints;
        if (ps.token.tk == tk_lsquare) {
            auto constraints_token = ps.token;
            ps.advance();
            while (ps.token.tk == tk_identifier) {
                auto ftv = iid(ps.token);

                if (in(ftv, generics)) {
                    auto iter = generics.find(ftv);
                    auto error = user_error(ftv.location,
                            "illegal redeclaration of type variable %s", 
                            ftv.str().c_str());
                    error.add_info((*iter).location, "see original declaration of type variable %s",
                            (*iter).str().c_str());
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

        type_t::ref type_args = parse_type_args(ps, generics, true /*automatic_any*/);

        types::type_t::ref return_type;
        /* now let's parse the return type */
        if (!ps.line_broke() && !(ps.token.tk == tk_expr_block || ps.token.tk == tk_lcurly || ps.token.tk == tk_rcurly)) {
            return_type = parse_type(ps, generics);
        } else {
            assert(default_return_type != nullptr);
            return_type = default_return_type;
        }

        auto type = type_function(location, type_constraints, type_args, return_type);
        if (name != nullptr) {
            return type;
        } else {
            return type_function_closure(type);
        }
    }

	type_t::ref parse_vector_type(parse_state_t &ps, const std::set<identifier_t> &generics) {
		/* we've got a map type */
		auto square_token = ps.token;
		ps.advance();
		auto lhs = parse_type(ps, generics);
		if (ps.token.tk == tk_colon) {
			ps.advance();
			auto rhs = parse_type(ps, generics);
			chomp_token(tk_rsquare);
			return type_operator(
					type_operator(type_id(identifier_t{STD_MAP_TYPE, square_token.location}), lhs),
					rhs);
		} else {
			chomp_token(tk_rsquare);
			return type_operator(type_id(identifier_t{STD_VECTOR_TYPE, square_token.location}), lhs);
		}
	}

	type_t::ref parse_integer_type(parse_state_t &ps, const std::set<identifier_t> &generics) {
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

	type_t::ref parse_lambda_type(parse_state_t &ps, const std::set<identifier_t> &generics) {
		if (ps.token.is_ident(K(lambda))) {
			ps.advance();
			expect_token(tk_identifier);
			auto param_token = ps.token;
			ps.advance();
			auto body = parse_and_type(ps, generics);
			return type_lambda(iid(param_token), body);
		} else if (ps.token.is_ident(K(fn))) {
			auto location = ps.token.location;
			ps.advance();
			std::shared_ptr<identifier_t> name;
			auto fn_type = parse_function_type(ps, location, generics, name, nullptr);
			if (name != nullptr && name->name != "_") {
				auto error = user_error(name->location, "function name unexpected in this context (" c_id("%s") ")",
						name->name.c_str());
				error.add_info(fn_type->get_location(), "while parsing type %s", fn_type->str().c_str());
				error.add_info(fn_type->get_location(), "note: to describe an unbound function type use the name '_'");
				throw error;
			}
			return fn_type;
		} else if (ps.token.is_ident(K(any))) {
			auto token = ps.token;
			ps.advance();

			type_t::ref type;
			if (!ps.line_broke() && ps.token.tk == tk_identifier && !token_is_illegal_in_type(ps.token)) {
				/* named generic */
				type = type_variable(iid(ps.token));
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

	type_t::ref parse_ptr_type(parse_state_t &ps, const std::set<identifier_t> &generics, bool disallow_maybe=false) {
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
					return type_operator(type_id(make_iid(MAYBE_TYPE)), element);
				}
			} else {
				return element;
			}
		}
	}

	type_t::ref parse_ref_type(parse_state_t &ps, const std::set<identifier_t> &generics) {
		bool is_ref = false;
		if (ps.token.tk == tk_ampersand) {
			ps.advance();
			is_ref = true;
		}
		auto element = parse_ptr_type(ps, generics);
		return is_ref ? type_ref(element) : element;
	}

	type_t::ref parse_application_type(parse_state_t &ps, const std::set<identifier_t> &generics) {
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

	type_t::ref parse_infix_subtype(parse_state_t &ps, const std::set<identifier_t> &generics) {
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

	type_t::ref parse_eq_type(parse_state_t &ps, const std::set<identifier_t> &generics) {
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

	type_t::ref parse_and_type(parse_state_t &ps, const std::set<identifier_t> &generics) {
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

	type_t::ref parse_or_type(parse_state_t &ps, const std::set<identifier_t> &generics) {
		return parse_and_type(ps, generics);
	}

	identifier_t reduce_ids(const std::list<identifier_t> &ids, location_t location) {
		assert(ids.size() != 0);
		return identifier_t{join(ids, SCOPE_SEP), location};
	}

	type_t::ref parse_type(parse_state_t &ps, const std::set<identifier_t> &generics) {
		assert(ps.token.tk != tk_lcurly && ps.token.tk != tk_rcurly);
		return types::parse_or_type(ps, generics);
	}
}

types::type_t::ref parse_type_expr(
		std::string input,
	   	std::set<identifier_t> generics,
	   	identifier_t module_id)
{
	std::istringstream iss(input);
	zion_lexer_t lexer("", iss);

	parse_state_t ps("", module_id.name, lexer, nullptr);
	debug_above(8, log("parsing %s", input.c_str()));
	return types::parse_type(ps, generics);
}

