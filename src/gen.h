#pragma once
#include <memory>
#include "ast.h"

namespace gen {
	struct value_t {
		typedef std::shared_ptr<value_t> ref;

		value_t(location_t location, types::type_t::ref type) : location(location), type(type) {}
		virtual ~value_t() {}
		virtual std::string str() const = 0;
		location_t get_location() const { return location; }

		location_t const location;
		types::type_t::ref const type;
	};

	struct literal_t : public value_t {
		std::string str() const override;
		literal_t(token_t token, types::type_t::ref type) : value_t(token.location, type), token(token) {}
		token_t const token;
	};

	struct block_t;

	struct instruction_t : public value_t {
		typedef std::shared_ptr<instruction_t> ref;

		instruction_t(location_t location, types::type_t::ref type, std::weak_ptr<block_t> parent) : value_t(location, type), parent(parent) {}

		virtual ~instruction_t() {}

		std::weak_ptr<block_t> parent;
	};

	typedef std::list<instruction_t::ref> instructions_t;

	struct block_t {
		typedef std::shared_ptr<block_t> ref;

		std::string str() const;

		instructions_t instructions;
	};

	struct argument_t;

	struct function_t : public value_t {
		typedef std::shared_ptr<function_t> ref;
		typedef std::weak_ptr<function_t> wref;

		function_t(std::string name, identifier_t param_id, location_t location, types::type_t::ref type) :
		   	value_t(location, type),
		   	name(name),
			param_id(param_id)
	   	{
		}
		friend function_t::ref gen_function(std::string name, identifier_t param_id, location_t location, types::type_t::ref type);

		std::string str() const override;

		std::string name;
		identifier_t param_id;
		std::vector<block_t::ref> blocks;
		std::vector<std::shared_ptr<argument_t>> args;
	};

	struct argument_t : public value_t {
		typedef std::shared_ptr<argument_t> ref;
		argument_t(location_t location, types::type_t::ref type, int index, function_t::wref function) :
		   	value_t(location, type),
		   	index(index),
			function(function)
		{}

		std::string str() const override;

		int index;
		function_t::wref function;
	};

	struct load_t : public instruction_t {
		std::string str() const override;
		value_t::ref rhs;
	};

	struct store_t : public instruction_t {
		std::string str() const override;
		value_t::ref lhs, rhs;
	};

	struct phi_node_t : public value_t {
		std::vector<std::pair<value_t::ref, block_t::ref>> incoming_values;
	};

	typedef std::unordered_map<std::string, value_t::ref> env_t;

	value_t::ref get_env_variable(const env_t &env, identifier_t id);

	struct module_t {
		env_t env;
	};

	struct builder_t {
		builder_t() {}
		builder_t(function_t::ref function) : function(function) {}

		void new_block(std::string name);

		builder_t save_ip() const;
		void restore_ip(const builder_t &builder);

		function_t::ref function;
		block_t::ref block;
		instructions_t::iterator insertion_point;
	};

	phi_node_t::ref phi_node(types::type_t::ref type);
	value_t::ref gen(builder_t &builder, const bitter::expr_t *expr, const tracked_types_t &typing, const env_t &env, const std::unordered_set<std::string> &globals);
}
