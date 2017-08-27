#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "signature.h"
#include "utils.h"
#include "identifier.h"

extern const char *STD_LIST_TYPE;
extern const char *BUILTIN_VOID_TYPE;
extern const char *BUILTIN_UNREACHABLE_TYPE;

/* Product Kinds */
enum product_kind_t {
	pk_module = 0,
	pk_args,
	pk_struct,
	pk_managed,
};

const char *pkstr(product_kind_t pk);

/* used to reset the generic type id counter */
void reset_generics();

namespace types {

	typedef atom::map<int> name_index_t;

	struct signature;

	struct type_t : public std::enable_shared_from_this<type_t> {
		typedef ptr<const type_t> ref;
		typedef std::vector<ref> refs;
		typedef std::map<atom, ref> map;
        typedef std::pair<ref, ref> pair;

		virtual ~type_t() {}
		virtual std::ostream &emit(std::ostream &os, const map &bindings) const = 0;

		/* how many free type variables exist in this type? NB: Assumes you have
         * already bound existing bindings at the callsite prior to this check. */
		virtual int ftv_count() const = 0;

        /* NB: Also assumes you have rebound the bindings at the callsite. */
		virtual atom::set get_ftvs() const = 0;

		atom repr(const map &bindings) const;
		atom repr() const { return this->repr({}); }

		virtual location_t get_location() const = 0;
		virtual identifier::ref get_id() const = 0;

		std::string str() const;
		std::string str(const map &bindings) const;
		atom get_signature() const { return repr(); }

		virtual ref rebind(const map &bindings) const = 0;

		virtual bool is_ref() const { not_impl(); return false; }
		virtual bool is_function() const { return false; }
		virtual bool is_void() const { return false; }
		virtual bool is_nil() const { return false; }
	};

	bool is_type_id(type_t::ref type, atom type_name);
	bool is_managed_ptr(types::type_t::ref type, types::type_t::map env);
	bool is_ptr(types::type_t::ref type, types::type_t::map env);

	struct type_id_t : public type_t {
		type_id_t(identifier::ref id);
		identifier::ref id;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
		virtual bool is_void() const;
		virtual bool is_nil() const;
	};

	struct type_variable_t : public type_t {
		type_variable_t(identifier::ref id);
		type_variable_t(location_t location /* auto-generated fresh type variables */);
		identifier::ref id;
		location_t location;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
	};

	struct type_operator_t : public type_t {
		type_operator_t(type_t::ref oper, type_t::ref operand);
		type_t::ref oper;
		type_t::ref operand;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
	};

	struct type_product_t : public type_t {
		typedef ptr<const type_product_t> ref;

		virtual product_kind_t get_pk() const = 0;
		virtual type_t::refs get_dimensions() const = 0;
		virtual name_index_t get_name_index() const = 0;
	};

	struct type_module_t : public type_product_t {
		typedef ptr<const type_module_t> ref;

		type_module_t(type_t::ref module_type);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;
		virtual name_index_t get_name_index() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;

		type_t::ref module_type;
	};

	struct type_managed_t : public type_product_t {
		typedef ptr<const type_managed_t> ref;

		type_managed_t(type_t::ref element_type);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;
		virtual name_index_t get_name_index() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;

		type_t::ref element_type;
	};

	struct type_args_t : public type_product_t {
		typedef ptr<const type_args_t> ref;

		type_args_t(type_t::refs args, name_index_t name_index);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;
		virtual name_index_t get_name_index() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;

		type_t::refs args;
		name_index_t name_index;
	};

	struct type_struct_t : public type_product_t {
		typedef ptr<const type_struct_t> ref;

		type_struct_t(type_t::refs dimensions, name_index_t name_index);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;
		virtual name_index_t get_name_index() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;

		type_t::refs dimensions;
		name_index_t name_index;
	};

	struct type_function_t : public type_t {
		typedef ptr<const type_function_t> ref;
		type_function_t(type_t::ref inbound_context, types::type_args_t::ref args, type_t::ref return_type);

		type_t::ref inbound_context;
		type_args_t::ref args;
		type_t::ref return_type;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;

		virtual bool is_function() const;
	};

	struct type_sum_t : public type_t {
		type_sum_t(type_t::refs options);
		type_t::refs options;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
	};

	struct type_maybe_t : public type_t {
		type_maybe_t(type_t::ref just);
		type_t::ref just;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
	};

	struct type_ptr_t : public type_t {
		typedef ptr<const type_ptr_t> ref;
		type_ptr_t(type_t::ref raw);
		type_t::ref element_type;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
	};

	struct type_ref_t : public type_t {
		typedef ptr<const type_ref_t> ref;
		type_ref_t(type_t::ref raw);
		type_t::ref element_type;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
	};

	struct type_lambda_t : public type_t {
		type_lambda_t(identifier::ref binding, type_t::ref body);
		identifier::ref binding;
		type_t::ref body;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual atom::set get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
	};

    identifier::ref gensym();
};

/* type data ctors */
types::type_t::ref type_nil();
types::type_t::ref type_void();
types::type_t::ref type_unreachable();
types::type_t::ref type_id(identifier::ref var);
types::type_t::ref type_variable(identifier::ref name);
types::type_t::ref type_variable(location_t location);
types::type_t::ref type_operator(types::type_t::ref operator_, types::type_t::ref operand);
types::type_module_t::ref type_module(types::type_t::ref module);
types::type_managed_t::ref type_managed(types::type_t::ref element);
types::type_struct_t::ref type_struct(types::type_t::refs dimensions, types::name_index_t name_index);
types::type_args_t::ref type_args(types::type_t::refs args, types::name_index_t name_index={});
types::type_function_t::ref type_function(types::type_t::ref inbound_context, types::type_args_t::ref args, types::type_t::ref return_type);
types::type_t::ref type_sum(types::type_t::refs options);
types::type_t::ref type_sum_safe(status_t &status, types::type_t::refs options);
types::type_t::ref type_maybe(types::type_t::ref just);
types::type_t::ref type_ptr(types::type_t::ref raw);
types::type_t::ref type_ref(types::type_t::ref raw);
types::type_t::ref type_lambda(identifier::ref binding, types::type_t::ref body);
types::type_t::ref type_list_type(types::type_t::ref element);
types::type_t::ref type_strip_maybe(types::type_t::ref maybe_maybe);

types::type_t::ref eval(types::type_t::ref type, types::type_t::map env);
types::type_t::ref eval_id(ptr<const types::type_id_t> ptid, types::type_t::map env);
types::type_t::ref eval_apply(types::type_t::ref oper, types::type_t::ref operand, types::type_t::map env);
bool type_is_unbound(types::type_t::ref type, types::type_t::map bindings);
std::ostream &operator <<(std::ostream &os, identifier::ref id);
std::string str(types::type_t::refs refs);
std::string str(types::type_t::map coll);
std::ostream& operator <<(std::ostream &out, const types::type_t::ref &type);

/* helper functions */
types::type_t::ref get_function_type_context(types::type_t::ref function_type);
types::type_t::ref get_function_return_type(types::type_t::ref function_type);
types::type_t::pair make_type_pair(std::string fst, std::string snd, identifier::set generics);

types::type_t::ref parse_type_expr(std::string input, identifier::set generics, identifier::ref module_id);
bool get_type_variable_name(types::type_t::ref type, atom &name);
std::ostream &join_dimensions(std::ostream &os, const types::type_t::refs &dimensions, const types::name_index_t &name_index, const types::type_t::map &bindings);
