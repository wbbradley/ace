#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "signature.h"
#include "utils.h"
#include "identifier.h"

extern const char *BUILTIN_LIST_TYPE;

/* Product Kinds */
enum product_kind_t {
	pk_obj = 0,
	pk_function,
	pk_args,
	pk_tuple,
	pk_tag,
	pk_tagged_tuple,
	pk_struct,
	pk_named_dimension,
};

const char *pkstr(product_kind_t pk);

/* used to reset the generic type id counter */
void reset_generics();

namespace types {

	struct signature;
	struct type_visitor;

	struct type : public std::enable_shared_from_this<type> {
		typedef ptr<const type> ref;
		typedef std::vector<ref> refs;
		typedef std::map<atom, ref> map;
        typedef std::pair<ref, ref> pair;

		virtual ~type() {}
		virtual std::ostream &emit(std::ostream &os, const map &bindings) const = 0;

		/* how many free type variables exist in this type? */
		virtual int ftv() const = 0;

		atom repr(const map &bindings) const;
		atom repr() const { return this->repr({}); }

		virtual location get_location() const = 0;

		std::string str(const map &bindings = {}) const;
		atom get_signature() const { return repr(); }

		virtual bool accept(type_visitor &visitor) const = 0;
		virtual ref rebind(const map &bindings) const = 0;

		virtual bool is_function() const { return false; }
		virtual bool is_void() const { return false; }
		virtual bool is_obj() const { return false; }
		virtual bool is_struct() const { return false; }
	};

	bool is_type_id(type::ref type, atom type_name);

	type::ref change_product_kind(product_kind_t pk, type::ref product);

	struct type_id : public type {
		type_id(identifier::ref id);
		identifier::ref id;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv() const;
		virtual bool accept(type_visitor &visitor) const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
		virtual bool is_void() const;
	};

	struct type_variable : public type {
		type_variable(identifier::ref id);
		identifier::ref id;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv() const;
		virtual bool accept(type_visitor &visitor) const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
	};

	struct type_operator : public type {
		type_operator(type::ref oper, type::ref operand);
		type::ref oper;
		type::ref operand;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv() const;
		virtual bool accept(type_visitor &visitor) const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
	};

	struct type_product : public type {
		type_product(product_kind_t pk, type::refs dimensions);
		product_kind_t pk;
		type::refs dimensions;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv() const;
		virtual bool accept(type_visitor &visitor) const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;

		virtual bool is_function() const;
		virtual bool is_obj() const;
		virtual bool is_struct() const;
	};

	struct type_sum : public type {
		type_sum(type::refs options);
		type::refs options;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv() const;
		virtual bool accept(type_visitor &visitor) const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;

		virtual bool is_obj() const { return true; }
	};
};

/* type data ctors */
types::type::ref type_unreachable();
types::type::ref type_id(identifier::ref var);
types::type::ref type_variable(identifier::ref name);
types::type::ref type_operator(types::type::ref operator_, types::type::ref operand);
types::type::ref type_product(product_kind_t pk, types::type::refs dimensions);
types::type::ref type_sum(types::type::refs options);

std::ostream &operator <<(std::ostream &os, identifier::ref id);
std::string str(types::type::refs refs);
std::string str(types::type::map coll);
std::ostream& operator <<(std::ostream &out, const types::type::ref &type);

/* helper functions */
types::type::ref get_args_type(types::type::refs args);
types::type::ref get_function_type(types::type::ref args, types::type::ref return_type);
types::type::refs get_function_type_args(types::type::ref function_type);
types::type::ref get_function_return_type(types::type::ref function_type);
types::type::ref get_obj_type(types::type::ref item);
types::type::pair make_type_pair(std::string fst, std::string snd, identifier::set generics);

types::type::ref operator "" _ty(const char *value, size_t);
types::type::ref parse_type_expr(std::string input, identifier::set generics);
bool get_type_variable_name(types::type::ref type, atom &name);
