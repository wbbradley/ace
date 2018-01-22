namespace types {
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
		while (!!ps.status && ps.token.tk != tk_outdent) {
			if (!ps.line_broke() && ps.prior_token.tk != tk_indent) {
				ps.error("product type dimensions must be separated by a newline");
			}

			token_t var_token;
			bool _mutable = false;
			if (ps.token.is_ident(K(var)) ||
					ps.token.is_ident(K(let)))
			{
				_mutable = ps.token.is_ident(K(var));

				ps.advance();
				expect_token(tk_identifier);
				var_token = ps.token;
				if (name_index.find(var_token.text) != name_index.end()) {
					ps.error("name " c_id("%s") " already exists in type", var_token.text.c_str());
				}
				name_index[var_token.text] = index++;
				ps.advance();
			} else {
				ps.error("not sure what's going on here");
				wat();
				expect_token(tk_identifier);
				var_token = ps.token;
			}

			type_t::ref dim_type = parse_maybe_type(ps, {}, {}, generics);
			if (_mutable) {
				dim_type = type_ref(dim_type);
			}

			if (!!ps.status) {
				dimensions.push_back(dim_type);
			}
		}
		chomp_token(tk_outdent);
		if (!!ps.status) {
			return ::type_struct(dimensions, name_index);
		}

		assert(!ps.status);
		return nullptr;
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
		ps.advance();
		auto lhs = parse_type(ps, generics);
		if (!!ps.status) {
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
					if (!!ps.status) {
						terms.push_back(next_term);
					} else {
						break;
					}
				}

				if (!!ps.status) {
					chomp_token(tk_rparen);
					return type_tuple(terms, lhs->get_location());
				}
			} else {
				/* we've got a single expression */
				return lhs;
			}
		}

		assert(!ps.status);
		return nullptr;
	}

	types::type_t::ref parse_function_type(parse_state_t &ps, const identifier::set &generics) {
		chomp_ident(K(def));
		chomp_token(tk_lparen);
		types::type_t::refs param_types;
		types::type_t::ref return_type;
		std::map<std::string, int> name_index;
		int index = 0;

		while (!!ps.status) {
			if (ps.token.tk == tk_identifier) {
				auto var_name = ps.token;
				ps.advance();
				types::type_t::ref type = parse_type(ps, generics);
				if (!!ps.status) {
					param_types.push_back(type);
					if (name_index.find(var_name.text) != name_index.end()) {
						ps.error("duplicated parameter name: %s",
								var_name.text.c_str());
					} else {
						name_index[var_name.text] = index;
						++index;
					}
					if (ps.token.tk == tk_comma) {
						/* advance past a comma */
						ps.advance();
					}
				}
			} else if (ps.token.tk == tk_rparen) {
				ps.advance();
				break;
			} else {
				ps.error("expected a parameter name");
				return nullptr;
			}
		}

		if (!!ps.status) {
			/* now let's parse the return type */
			if (!ps.line_broke() && token_begins_type(ps.token)) {
				return_type = parse_type(ps, generics);
			} else {
				return_type = type_void();
			}

			if (!!ps.status) {
				return type_function(type_args(param_types), return_type);
			}
		}

		assert(!ps.status);
		return nullptr;
	}

	type_t::ref parse_map_type(parse_state_t &ps, const identifier::set &generics) {
		/* we've got a map type */
		auto curly_token = ps.token;
		ps.advance();
		auto lhs = parse_type(ps, generics);
		if (!!ps.status) {
			chomp_token(tk_colon);
			ps.advance();
			auto rhs = parse_type(ps, generics);
			if (!!ps.status) {
				chomp_token(tk_rcurly);
				return type_operator(
						type_operator(type_id(make_iid_impl("map.map", curly_token.location)), lhs),
						rhs);
			}
		}

		assert(!ps.status);
		return nullptr;
	}

	type_t::ref parse_vector_type(parse_state_t &ps, const identifier::set &generics) {
		/* we've got a map type */
		auto square_token = ps.token;
		ps.advance();
		auto lhs = parse_type(ps, generics);
		if (!!ps.status) {
			chomp_token(tk_rsquare);
			return type_operator(type_id(make_iid_impl("vector.vector", square_token.location)), lhs);
		}

		assert(!ps.status);
		return nullptr;
	}

	type_t::ref parse_lambda_type(parse_state_t &ps, const identifier::set &generics) {
		if (ps.token.is_ident(K(lambda))) {
			ps.advance();
			expect_token(tk_identifier);
			auto param_token = ps.token;
			ps.advance();
			auto body = parse_and_type(ps, generics);
			if (!!ps.status) {
				return type_lambda(make_code_id(param_token), body);
			}
		} else if (ps.token.is_ident(K(def))) {
			return parse_function_type(ps, generics);
		} else if (ps.token.is_ident(K(any))) {
			auto token = ps.token;
			ps.advance();

			if (ps.token.tk == tk_identifier) {
				auto var = ps.token;
				ps.advance();
				type_t::ref type;
				if (ps.token.tk == tk_identifier) {
					/* named generic */
					type = type_variable(make_code_id(ps.token));
					ps.advance();
				} else {
					/* no named generic */
					type = type_variable(var.location);
				}
				return type;
			}
		} else if (ps.token.is_ident(K(struct))) {
			return parse_product_type(ps, generics);
		} else if (ps.token.is_ident(K(has))) {
			return parse_product_type(ps, generics);
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
			return parse_identifier_type(ps, generics);
		}
	}

	type_t::ref parse_optional_type(parse_state_t &ps, const identifier::set &generics) {
		auto lhs = parse_lambda_type(ps, generics);
		if (!!ps.status) {
			if (lhs == nullptr) {
				/* this is valid. it is telling the parser that there is not another higher
				 * precedence type. */
				return nullptr;
			} else {
				if (ps.token.tk == tk_maybe) {
					return type_maybe(lhs);
				} else {
					return lhs;
				}
			}
		}
		assert(!ps.status);
		return nullptr;
	}

	type_t::ref parse_application_type(parse_state_t &ps, const identifier::set &generics) {
		auto lhs = parse_optional_type(ps, generics);
		if (!!ps.status) {
			if (lhs == nullptr) {
				ps.error("unable to parse type");
			} else {
				std::vector<type_t::ref> terms;
				terms.push_back(lhs);
				while (auto next_term = parse_optional_type(ps, generics)) {
					terms.push_back(next_term);
				}

				if (!!ps.status) {
					for (int i=0; i<terms.size(); ++i) {
						lhs = type_operator(lhs, terms[i]);
					}
					return lhs;
				}
			}
		}
		assert(!ps.status);
		return nullptr;
	}

	type_t::ref parse_ptr_type(parse_state_t &ps, const identifier::set &generics) {
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
		auto element = parse_application_type(ps, generics);
		if (!!ps.status) {
			if (is_maybe) {
				return type_maybe(type_ptr(element));
			} else if (is_ptr) {
				return type_ptr(element);
			} else {
				return element;
			}
		}
		assert(!ps.status);
		return nullptr;
	}

	type_t::ref parse_ref_type(parse_state_t &ps, const identifier::set &generics) {
		bool is_ref = false;
		if (ps.token.tk == tk_ampersand) {
			ps.advance();
			is_ref = true;
		}
		auto element = parse_ptr_type(ps, generics);
		if (!!ps.status) {
			return is_ref ? type_ref(element) : element;
		}
		assert(!ps.status);
		return nullptr;
	}

	type_t::ref parse_and_type(parse_state_t &ps, const identifier::set &generics) {
		auto lhs = parse_ref_type(ps, generics);
		if (!!ps.status) {
			if (ps.token.is_ident(K(and))) {
				/* we've got a Logical AND expression */
				std::vector<type_t::ref> terms;
				terms.push_back(lhs);
				while (ps.token.is_ident(K(and))) {
					chomp_ident(K(and));
					auto next_term = parse_ref_type(ps, generics);
					if (!!ps.status) {
						terms.push_back(next_term);
					} else {
						break;
					}
				}

				if (!!ps.status) {
					return type_and(terms, lhs->get_location());
				}
			} else {
				/* we've got a single expression */
				return lhs;
			}
		}

		assert(!ps.status);
		return nullptr;
	}

	type_t::ref parse_type(parse_state_t &ps, const identifier::set &generics) {
		return parse_and_type(ps, supertype_id, type_variables, generics);
	}

	{
		location_t location = ps.token.location;
		type_t::refs options;
		while (!!ps.status) {
			auto type = _parse_single_type(ps, supertype_id, type_variables, generics);

			if (!!ps.status) {
				options.push_back(type);
				if (ps.token.is_ident(K(or))) {
					ps.advance();
					continue;
				} else {
					break;
				}
			}
		}

		if (!!ps.status) {
			if (options.size() == 1) {
				return options[0];
			} else {
				if (supertype_id != nullptr && supertype_id->get_name() == "Bool") {
					/* hack because it's not actually possible to define Bool in the language with
					 * automatically reducing types */
					return type_sum(options, supertype_id != nullptr ? supertype_id->get_location() : location);
				}

				type_t::ref sum_fn = type_sum_safe(options,
						supertype_id != nullptr ? supertype_id->get_location() : location,
						{});
				assert(sum_fn != nullptr);
				if (!!ps.status) {
					for (auto iter = type_variables.rbegin();
							iter != type_variables.rend();
							++iter)
					{
						sum_fn = ::type_lambda(*iter, sum_fn);
					}

					return sum_fn;
				}
			}
		}
		assert(!ps.status);
		return nullptr;
	}


	type_t::ref parse_maybe_type(
			parse_state_t &ps,
			identifier::ref supertype_id,
			identifier::refs type_variables,
			identifier::set generics)
	{
		type_t::ref type = _parse_type(ps, supertype_id, type_variables, generics);
		if (!!ps.status) {
			if (ps.token.tk == tk_maybe) {
				if (dyncast<const type_ptr_t>(type)) {
					ps.error("pointer types should be marked as maybe types immediately after the *");
				}
				if (!!ps.status) {
					/* no named maybe generic */
					type = type_maybe(type);
					ps.advance();
					return type;
				}
			} else {
				return type;
			}
		}
		assert(!ps.status);
		return nullptr;
	}


	identifier::ref reduce_ids(std::list<identifier::ref> ids, location_t location) {
		assert(ids.size() != 0);
		return make_iid_impl(join(ids, SCOPE_SEP), location);
	}

	type_t::ref parse_type_constraints(parse_state_t &ps, const identifier::set &generics) {
		expect_ident(K(where));
		location_t where_location = ps.token.location;
		ps.advance();

		return parse_type_constraints_expr(ps, generics);
	}


	type_t::ref parse_type_constraints_expr(
			parse_state_t &ps,
			const identifier::set &generics)
	{
		type_t::ref type = _parse_type(ps, nullptr, {}, generics);

		if (ps.token.is_ident(K(not))) {
			auto token = ps.token;

			assert(false);
		}

		assert(!ps.status);
		return nullptr;
	}

	type_t::ref _parse_single_type(
			parse_state_t &ps,
			identifier::ref supertype_id,
			identifier::refs type_variables,
			identifier::set generics)
	{
		if (!token_begins_type(ps.token)) {
			ps.error("type references cannot begin with %s", ps.token.str().c_str());
			return nullptr;
		}

		switch (ps.token.tk) {
		case tk_integer:
		case tk_string:
			{
				auto type = type_literal(ps.token);
				ps.advance();
				return type;
			}

		case tk_times:
			{
				ps.advance();
				bool is_maybe = false;
				if (ps.token.tk == tk_maybe) {
					is_maybe = true;
					ps.advance();
				}

				auto type = _parse_single_type(ps, supertype_id, type_variables, generics);
				if (!!ps.status) {
					return is_maybe ? ::type_maybe(::type_ptr(type)) : ::type_ptr(type);
				}
			}
			break;
		case tk_identifier:
			if (ps.token.is_ident(K(integer_t))) {
				auto token = ps.token;
				ps.advance();
				chomp_token(tk_lcurly);
				auto bit_size = _parse_single_type(ps, nullptr, type_variables, generics);
				if (!!ps.status) {
					chomp_token(tk_comma);
					auto signed_ = _parse_single_type(ps, nullptr, type_variables, generics);
					if (!!ps.status) {
						chomp_token(tk_rcurly);
						return type_integer(bit_size, signed_);
					}
				}

				assert(!ps.status);
				return nullptr;
			} else if (ps.token.is_ident(K(has))
					|| ps.token.is_ident(K(struct)))
			{
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
				while (!!ps.status && ps.token.tk != tk_outdent) {
					if (!ps.line_broke() && ps.prior_token.tk != tk_indent) {
						ps.error("product type dimensions must be separated by a newline");
					}

					token_t var_token;
					bool _mutable = false;
					if (ps.token.is_ident(K(var)) ||
							ps.token.is_ident(K(let)))
					{
						_mutable = ps.token.is_ident(K(var));

						ps.advance();
						expect_token(tk_identifier);
						var_token = ps.token;
						if (name_index.find(var_token.text) != name_index.end()) {
							ps.error("name " c_id("%s") " already exists in type", var_token.text.c_str());
						}
						name_index[var_token.text] = index++;
						ps.advance();
					} else {
						ps.error("not sure what's going on here");
						wat();
						expect_token(tk_identifier);
						var_token = ps.token;
					}

					type_t::ref dim_type = parse_maybe_type(ps, {}, {}, generics);
					if (_mutable) {
						dim_type = type_ref(dim_type);
					}

					if (!!ps.status) {
						dimensions.push_back(dim_type);
					}
				}
				chomp_token(tk_outdent);
				if (!!ps.status) {
					return ::type_struct(dimensions, name_index);
				} else {
					return nullptr;
				}
			} else if (ps.token.is_ident(K(def))) {
				return _parse_function_type(ps, generics);
			} else if (ps.token.is_ident(K(any))) {
				/* parse generic refs */
				ps.advance();
				type_t::ref type;
				if (ps.token.tk == tk_identifier) {
					/* named generic */
					type = type_variable(make_code_id(ps.token));
					ps.advance();
				} else {
					/* no named generic */
					type = type_variable(ps.token.location);
				}
				return type;
			} else {
				/* build the type-path that is referenced here */
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
					cur_type = type_variable(id);
				} else {
					/* this is not a generic */
					if (in(id->get_name(), ps.type_macros)) {
						debug_above(9, log("checking whether type " c_id("%s") " expands...",
									id->get_name().c_str()));

						/* macro type expansion */
						cur_type = ps.type_macros[id->get_name()];
					} else if (id->get_name().find(SCOPE_SEP_CHAR) != std::string::npos) {
						/* if we're explicit about the type path, then let's just
						 * use that as the id */
						cur_type = type_id(id);
					} else {
						/* we don't have a macro/type_name link for this type, so
						 * let's assume it's in this module */
						if (ps.module_id->get_name() == GLOBAL_SCOPE_NAME) {
							/* the std module is the only "global" module */
							cur_type = type_id(id);
						} else {
							assert(ps.module_id->get_name().size() != 0);
							cur_type = type_id(reduce_ids({ps.module_id, id}, location));
						}
						debug_above(9, log("transformed " c_id("%s") " to " c_id("%s"),
									id->get_name().c_str(),
									cur_type->str().c_str()));
					}
				}

				type_t::refs arguments;
				if (ps.token.tk == tk_lcurly) {
					ps.advance();
					arguments = parse_type_operands(ps, supertype_id,
							type_variables, generics);
				}

				for (auto type_arg : arguments) {
					cur_type = type_operator(cur_type, type_arg);
				}

				return cur_type;
			}
			break;
		case tk_lsquare:
			{
				ps.advance();
				auto element_type = parse_maybe_type(ps, supertype_id,
						type_variables, generics);

				if (ps.token.tk != tk_rsquare) {
					ps.error("vector type reference must end with a ']', found %s",
							ps.token.str().c_str());
					return nullptr;
				} else {
					ps.advance();
					return type_vector_type(element_type);
				}
			}
			break;
		case tk_lcurly:
			{
				ps.advance();
				type_t::refs arguments = parse_type_operands(ps, supertype_id, type_variables, generics);
				return type_tuple(arguments);
			}
			break;
		default:
			panic("should have been caught above.");
		}

		not_impl();
		return nullptr;
	}
}
