#pragma once
#include <memory>
#include "ast.h"

namespace gen {
	struct value_t {
		typedef std::shared_ptr<value_t> ref;
		typedef std::vector<ref> refs;

		value_t(location_t location, types::type_t::ref type) : location(location), type(type) {}
		virtual ~value_t() {}
		virtual std::string str() const = 0;
		location_t get_location() const { return location; }

		location_t const location;
		types::type_t::ref const type;
	};

	typedef std::unordered_map<std::string, value_t::ref> env_t;
	value_t::ref get_env_variable(const env_t &env, identifier_t id);
	void set_env_var(env_t &env, std::string name, value_t::ref value);

	struct module_t {
		typedef std::shared_ptr<module_t> ref;

		env_t env;
	};

	struct literal_t : public value_t {
		std::string str() const override;
		literal_t(token_t token, types::type_t::ref type) : value_t(token.location, type), token(token) {}
		token_t const token;
	};

	struct block_t;
	struct function_t;

	struct instruction_t : public value_t {
		typedef std::shared_ptr<instruction_t> ref;

		std::string str() const override final;
		virtual std::string get_value_name(location_t location) const;
		virtual std::ostream &render(std::ostream &os) const = 0;

		instruction_t(location_t location, types::type_t::ref type, std::weak_ptr<block_t> parent) : value_t(location, type), parent(parent) {}

		virtual ~instruction_t() {}

		std::weak_ptr<block_t> parent;
	};

	typedef std::list<instruction_t::ref> instructions_t;

	struct block_t {
		typedef std::shared_ptr<block_t> ref;

		block_t(std::weak_ptr<function_t> parent, std::string name) : parent(parent), name(name) {}
		std::string str() const;

		std::weak_ptr<function_t> parent;
		std::string const name;
		instructions_t instructions;
	};

	struct cast_t : public instruction_t {
		cast_t(location_t location, std::weak_ptr<block_t> parent, value_t::ref value, types::type_t::ref type) :
			instruction_t(location, type, parent),
			lhs_name(bitter::fresh()),
			value(value)
		{}
			
		std::string get_value_name(location_t location) const override;
		std::ostream &render(std::ostream &os) const override;

		std::string lhs_name;
		value_t::ref value;

	};

	struct argument_t;

	struct function_t : public value_t {
		typedef std::shared_ptr<function_t> ref;
		typedef std::weak_ptr<function_t> wref;

		function_t(module_t::ref module, std::string name, location_t location, types::type_t::ref type) :
			value_t(location, type),
			parent(module),
			name(name)
		{
		}

		std::string str() const override;
		std::ostream &render(std::ostream &os) const;

		std::weak_ptr<module_t> parent;
		std::string name;
		std::vector<block_t::ref> blocks;
		std::vector<std::shared_ptr<argument_t>> args;
	};

	struct builtin_t : public instruction_t {
		typedef std::shared_ptr<builtin_t> ref;

		builtin_t(location_t location, std::weak_ptr<block_t> parent, identifier_t id, value_t::refs values, types::type_t::ref type) :
			instruction_t(location, type, parent),
			lhs_name(bitter::fresh()),
			id(id),
			values(values)
		{}

		std::string get_value_name(location_t location) const override;
		std::ostream &render(std::ostream &os) const override;

		std::string lhs_name;
		identifier_t id;
		value_t::refs values;
	};

	struct argument_t : public value_t {
		typedef std::shared_ptr<argument_t> ref;
		argument_t(identifier_t id, types::type_t::ref type, int index, function_t::wref function) :
		   	value_t(id.location, type),
			name(id.name),
		   	index(index),
			function(function)
		{}

		std::string str() const override;

		std::string name;
		int index;
		function_t::wref function;
	};

	struct return_t : public instruction_t {
		return_t(location_t location, std::weak_ptr<block_t> parent, value_t::ref value) :
			instruction_t(location, type_unit(INTERNAL_LOC()), parent),
			value(value)
		{}

		std::ostream &render(std::ostream &os) const override;

		value_t::ref value;
	};

	struct load_t : public instruction_t {
		load_t(location_t location, std::weak_ptr<block_t> parent, value_t::ref rhs) :
			instruction_t(location, type_deref(rhs->type), parent),
			lhs_name(bitter::fresh()),
			rhs(rhs)
		{}

		std::ostream &render(std::ostream &os) const override;
		std::string get_value_name(location_t location) const override;

		std::string lhs_name;
		value_t::ref rhs;
	};

	struct store_t : public instruction_t {
		std::ostream &render(std::ostream &os) const override;

		store_t(location_t location, std::weak_ptr<block_t> parent, value_t::ref lhs, value_t::ref rhs) :
			instruction_t(location, type_bottom(), parent),
			lhs(lhs),
			rhs(rhs)
		{}
		value_t::ref lhs, rhs;
	};

	struct phi_node_t : public instruction_t {
		std::ostream &render(std::ostream &os) const override;
		std::vector<std::pair<value_t::ref, block_t::ref>> incoming_values;
	};

	types::type_t::ref tuple_type(const std::vector<value_t::ref> &dims);

	struct tuple_t : public instruction_t {
		std::ostream &render(std::ostream &os) const override;
		tuple_t(location_t location, std::weak_ptr<block_t> parent, std::vector<value_t::ref> dims) :
		   	instruction_t(location, tuple_type(dims), parent),
			lhs_name(bitter::fresh()),
		   	dims(dims)
		{
		}
		std::string get_value_name(location_t location) const override;

		std::string lhs_name;
		std::vector<value_t::ref> dims;
	};

	struct tuple_deref_t : public instruction_t {
		tuple_deref_t(location_t location, std::weak_ptr<block_t> parent, value_t::ref value, int index) :
		   	instruction_t(location, tuple_deref_type(location, value->type, index), parent),
			lhs_name(bitter::fresh()),
			value(value),
			index(index)
		{
		}
		std::ostream &render(std::ostream &os) const override;
		std::string get_value_name(location_t location) const override;

		std::string lhs_name;
		value_t::ref value;
		int index;
	};

	struct builder_t {
		typedef gen::builder_t saved_state;
		builder_t(module_t::ref module) : module(module) {}
		builder_t(function_t::ref function) : module(function->parent), function(function) {}

		block_t::ref create_block(std::string name);

		builder_t save_ip() const;
		void restore_ip(const builder_t &builder);

		value_t::ref create_builtin(identifier_t id, const value_t::refs &values, types::type_t::ref type);
		value_t::ref create_literal(token_t token, types::type_t::ref type);
		value_t::ref create_call(value_t::ref callable, const std::vector<value_t::ref> params);
		value_t::ref create_cast(location_t location, value_t::ref value, types::type_t::ref type);
		value_t::ref create_tuple(location_t location, const std::vector<value_t::ref> &dims);
		value_t::ref create_tuple_deref(location_t location, value_t::ref value, int index);
		value_t::ref create_branch(block_t::ref block);
		value_t::ref create_return(value_t::ref expr);
		function_t::ref create_function(std::string name, identifiers_t param_ids, location_t location, types::type_t::ref type);
		void insert_instruction(instruction_t::ref instruction);

	public:
		module_t::ref module;
		function_t::ref function;
		block_t::ref block;
		// std::shared_ptr<std::iterator<std::output_iterator_tag, void, void, void, void>> inserter;
	};

	phi_node_t::ref phi_node(types::type_t::ref type);
	value_t::ref gen(builder_t &builder, const bitter::expr_t *expr, const tracked_types_t &typing, const env_t &env, const std::unordered_set<std::string> &globals);
}
