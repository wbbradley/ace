#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "signature.h"
#include "utils.h"
#include "identifier.h"

extern const char *BUILTIN_LIST_TYPE;
extern const char *BUILTIN_VOID_TYPE;
extern const char *BUILTIN_UNREACHABLE_TYPE;

/* Product Kinds */
enum product_kind_t {
	pk_module = 0,
	pk_args,
	pk_tuple,
	pk_tag,
	pk_tagged_tuple,
	pk_struct,
};

const char *pkstr(product_kind_t pk);

/* used to reset the generic type id counter */
void reset_generics();

namespace types {

	typedef atom::map<int> name_index;

	struct signature;

	struct type : public std::enable_shared_from_this<type> {
		typedef ptr<const type> ref;
		typedef std::vector<ref> refs;
		typedef std::map<atom, ref> map;
        typedef std::pair<ref, ref> pair;

		virtual ~type() {}
		virtual std::ostream &emit(std::ostream &os, const map &bindings) const = 0;

		/* how many free type variables exist in this type? NB: Assumes you have
         * already bound existing bindings at the callsite prior to this check. */
		virtual int ftv_count() const = 0;

        /* NB: Also assumes you have rebound the bindings at the callsite. */
		virtual atom::set get_ftvs() const = 0;

		atom repr(const map &bindings) const;
		atom repr() const { return this->repr({}); }

		virtual location get_location() const = 0;
		virtual identifier::ref get_id() const = 0;

		std::string str() const;
		std::string str(const map &bindings) const;
		atom get_signature() const { return repr(); }

		virtual ref rebind(const map &bindings) const = 0;

		virtual bool is_function() const { return false; }
		virtual bool is_void() const { return false; }
		virtual bool is_struct() const { return false; }
		virtual bool is_nil() const { return false; }
	};

	bool is_type_id(type::ref type, atom type_name);

	type::ref change_product_kind(product_kind_t pk, type::ref product);

	struct type_id : public type {
		type_id(identifier::ref id);
		identifier::ref id;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
		virtual identifier::ref get_id() const;
		virtual bool is_void() const;
		virtual bool is_nil() const;
	};

	struct type_variable : public type {
		type_variable(identifier::ref id);
		type_variable(/* auto-generated fresh type variables */);
		identifier::ref id;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
		virtual identifier::ref get_id() const;
	};

	struct type_operator : public type {
		type_operator(type::ref oper, type::ref operand);
		type::ref oper;
		type::ref operand;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
		virtual identifier::ref get_id() const;
	};

	struct type_product : public type {

		type_product(product_kind_t pk, type::refs dimensions, const name_index &name_index);
		product_kind_t pk;
		type::refs dimensions;
		name_index name_index;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
		virtual identifier::ref get_id() const;

		virtual bool is_struct() const;
	};

	struct type_function : public type {

		type_function(type::ref inbound_context, type::ref args, type::ref return_type);
		type::ref inbound_context;
		type::ref args;
		type::ref return_type;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
		virtual identifier::ref get_id() const;

		virtual bool is_function() const;
	};

	struct type_sum : public type {
		type_sum(type::refs options);
		type::refs options;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
		virtual identifier::ref get_id() const;
	};

	struct type_maybe : public type {
		type_maybe(type::ref just);
		type::ref just;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
		virtual identifier::ref get_id() const;
	};

	struct type_lambda : public type {
		type_lambda(identifier::ref binding, type::ref body);
		identifier::ref binding;
		type::ref body;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location get_location() const;
		virtual identifier::ref get_id() const;
	};

    identifier::ref gensym();
};

/* type data ctors */
types::type::ref type_nil();
types::type::ref type_void();
types::type::ref type_unreachable();
types::type::ref type_id(identifier::ref var);
types::type::ref type_variable(identifier::ref name);
types::type::ref type_variable();
types::type::ref type_operator(types::type::ref operator_, types::type::ref operand);
types::type::ref type_product(product_kind_t pk, types::type::refs dimensions, const types::name_index &name_index={});
types::type::ref type_function(types::type::ref inbound_context, types::type::ref args, types::type::ref return_type);
types::type::ref type_sum(types::type::refs options);
types::type::ref type_maybe(types::type::ref just);
types::type::ref type_lambda(identifier::ref binding, types::type::ref body);
types::type::ref type_list_type(types::type::ref element);
types::type::ref type_strip_maybe(types::type::ref maybe_maybe);

types::type::ref eval_id(ptr<const types::type_id> ptid, types::type::map env);
types::type::ref eval_apply(types::type::ref oper, types::type::ref operand, types::type::map env);
bool type_is_unbound(types::type::ref type, types::type::map bindings);
std::ostream &operator <<(std::ostream &os, identifier::ref id);
std::string str(types::type::refs refs);
std::string str(types::type::map coll);
std::ostream& operator <<(std::ostream &out, const types::type::ref &type);

/* helper functions */
types::type::ref get_args_type(types::type::refs args);
types::type::ref get_function_type(types::type::ref type_fn_context, types::type::ref args, types::type::ref return_type);
types::type::ref get_function_type_context(types::type::ref function_type);
types::type::ref get_function_type_args(types::type::ref function_type);
types::type::refs get_function_type_args_dimensions(types::type::ref function_type);
types::type::ref get_function_return_type(types::type::ref function_type);
types::type::pair make_type_pair(std::string fst, std::string snd, identifier::set generics);

types::type::ref operator "" _ty(const char *value, size_t);
types::type::ref parse_type_expr(std::string input, identifier::set generics);
bool get_type_variable_name(types::type::ref type, atom &name);
