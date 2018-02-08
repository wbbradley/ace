#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "signature.h"
#include "utils.h"
#include "identifier.h"
#include "token.h"

extern const char *STD_VECTOR_TYPE;
extern const char *STD_MAP_TYPE;
extern const char *BUILTIN_VOID_TYPE;
extern const char *BUILTIN_UNREACHABLE_TYPE;

/* Product Kinds */
enum product_kind_t {
	pk_module = 0,
	pk_args,
	pk_tuple,
	pk_struct,
	pk_managed,
};

const char *pkstr(product_kind_t pk);

/* used to reset the generic type id counter */
void reset_generics();

namespace types {

	typedef std::map<std::string, int> name_index_t;

	struct signature;

	struct type_t : public std::enable_shared_from_this<type_t> {
		typedef ptr<const type_t> ref;
		typedef std::vector<ref> refs;
		typedef std::map<std::string, ref> map;
        typedef std::pair<ref, ref> pair;

		virtual ~type_t() {}
		virtual std::ostream &emit(std::ostream &os, const map &bindings) const = 0;

		/* how many free type variables exist in this type? NB: Assumes you have
         * already bound existing bindings at the callsite prior to this check. */
		virtual int ftv_count() const = 0;

        /* NB: Also assumes you have rebound the bindings at the callsite. */
		virtual std::set<std::string> get_ftvs() const = 0;

		std::string repr(const map &bindings) const;
		std::string repr() const { return this->repr({}); }

		virtual location_t get_location() const = 0;
		virtual identifier::ref get_id() const = 0;

		std::string str() const;
		std::string str(const map &bindings) const;
		std::string get_signature() const { return repr(); }

		virtual ref rebind(const map &bindings) const = 0;
		virtual ref eval_expr(const map &nominal_env, const map &structural_env, bool allow_structural_env) const;

        virtual type_t::ref boolean_refinement(bool elimination_value, types::type_t::map env) const;
		virtual bool is_ref() const { return false; }
		virtual bool is_function() const { return false; }
		virtual bool is_void() const { return false; }
		virtual bool is_null() const { return false; }
		virtual bool is_true() const { return false; }
		virtual bool is_false() const { return false; }
		virtual bool is_zero() const { return false; }
		virtual bool is_maybe() const { return false; }
		virtual int get_precedence() const { return 10; }
	};

	bool is_type_id(type_t::ref type, std::string type_name);
	bool is_managed_ptr(types::type_t::ref type, types::type_t::map env);
	bool is_ptr(types::type_t::ref type, types::type_t::map env);

	struct type_id_t : public type_t {
		type_id_t(identifier::ref id);
		identifier::ref id;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
		virtual type_t::ref eval_expr(const map &nominal_env, const map &structural_env, bool allow_structural_env) const;
		virtual bool is_void() const;
		virtual bool is_null() const;
		virtual bool is_zero() const;
		virtual bool is_true() const;
		virtual bool is_false() const;
        virtual type_t::ref boolean_refinement(bool elimination_value, types::type_t::map env) const;
	};

	struct type_variable_t : public type_t {
		type_variable_t(identifier::ref id);
		type_variable_t(location_t location /* auto-generated fresh type variables */);
		identifier::ref id;
		location_t location;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
	};

	struct type_operator_t : public type_t {
		type_operator_t(type_t::ref oper, type_t::ref operand);
		type_t::ref oper;
		type_t::ref operand;

		virtual int get_precedence() const { return 7; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
		virtual type_t::ref eval_expr(const map &nominal_env, const map &structural_env, bool allow_structural_env) const;
        virtual type_t::ref boolean_refinement(bool elimination_value, types::type_t::map env) const;
	};

	struct type_product_t : public type_t {
		typedef ptr<const type_product_t> ref;

		virtual product_kind_t get_pk() const = 0;
		virtual type_t::refs get_dimensions() const = 0;
	};


	struct type_literal_t : public type_t {
		type_literal_t(token_t token);
		token_t token;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
		int coerce_to_int(status_t &status) const;
	};

	struct type_integer_t : public type_t {
		type_integer_t(type_t::ref bit_size, type_t::ref signed_);
		type_t::ref bit_size;
		type_t::ref signed_;

		virtual int get_precedence() const { return 9; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
		type_t::ref boolean_refinement(bool elimination_value, types::type_t::map env) const;
	};

	struct type_module_t : public type_product_t {
		typedef ptr<const type_module_t> ref;

		virtual int get_precedence() const { return 0; }

		type_module_t(type_t::ref module_type);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
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

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;

		type_t::ref element_type;
	};

	struct type_args_t : public type_product_t {
		typedef ptr<const type_args_t> ref;

		type_args_t(type_t::refs args, identifier::refs names);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;

		type_t::refs args;
		identifier::refs names;
	};

	struct type_struct_t : public type_product_t {
		typedef ptr<const type_struct_t> ref;

		type_struct_t(type_t::refs dimensions, name_index_t name_index);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;

		type_t::refs dimensions;
		name_index_t name_index;
	};

	struct type_tuple_t : public type_product_t {
		typedef ptr<const type_tuple_t> ref;

		type_tuple_t(type_t::refs dimensions);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;

		type_t::refs dimensions;
	};

	struct type_function_t : public type_t {
		typedef ptr<const type_function_t> ref;
		type_function_t(
				identifier::ref name,
			   	types::type_t::ref type_constraints,
				types::type_t::ref args,
			   	type_t::ref return_type);

		identifier::ref name;
		type_t::ref type_constraints;
		type_t::ref args;
		type_t::ref return_type;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;

		virtual bool is_function() const;
	};

	struct type_and_t : public type_t {
		type_and_t(type_t::refs terms);
		type_t::refs terms;
		location_t location;

		virtual int get_precedence() const { return 5; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
		virtual ref eval_expr(const map &nominal_env, const map &structural_env, bool allow_structural_env) const;
	};

	struct type_sum_t : public type_t {
		type_sum_t(type_t::refs options);
		type_sum_t(type_t::refs options, location_t location);
		type_t::refs options;
		location_t location;

		virtual int get_precedence() const { return 4; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
		virtual ref eval_expr(const map &nominal_env, const map &structural_env, bool allow_structural_env) const;
        virtual type_t::ref boolean_refinement(bool elimination_value, types::type_t::map env) const;
	};

	struct type_maybe_t : public type_t {
		type_maybe_t(type_t::ref just);
		type_t::ref just;

		virtual int get_precedence() const { return 8; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
        virtual type_t::ref boolean_refinement(bool elimination_value, types::type_t::map env) const;
		virtual bool is_maybe() const { return true; }
	};

	struct type_ptr_t : public type_t {
		typedef ptr<const type_ptr_t> ref;
		type_ptr_t(type_t::ref raw);
		type_t::ref element_type;

		virtual int get_precedence() const { return 3; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
        virtual type_t::ref boolean_refinement(bool elimination_value, types::type_t::map env) const;
	};

	struct type_ref_t : public type_t {
		typedef ptr<const type_ref_t> ref;
		type_ref_t(type_t::ref raw);
		type_t::ref element_type;

		virtual int get_precedence() const { return 2; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;

		virtual bool is_ref() const { return true; }
	};

	struct type_lambda_t : public type_t {
		type_lambda_t(identifier::ref binding, type_t::ref body);
		identifier::ref binding;
		type_t::ref body;

		virtual int get_precedence() const { return 6; }
		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
	};

	struct type_extern_t : public type_t {
		typedef ptr<const type_extern_t> ref;
		type_extern_t(type_t::ref inner);
		type_t::ref inner;

		virtual std::ostream &emit(std::ostream &os, const map &bindings) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings) const;
		virtual location_t get_location() const;
		virtual identifier::ref get_id() const;
	};


	identifier::ref gensym();
	int coerce_to_integer(
			status_t &status,
			const types::type_t::map &env,
			type_t::ref type,
			type_t::ref &expansion);
	bool is_integer(type_t::ref type, const type_t::map &env);
	void get_integer_attributes(status_t &status, type_t::ref type, const type_t::map &env, unsigned &bit_size, bool &signed_);
	void get_runtime_typeids(status_t &status, type_t::ref type, const type_t::map &env, std::set<int> &typeids);
	type_t::ref without_ref(type_t::ref type);
	type_t::refs without_refs(type_t::refs types);
};

/* type data ctors */
types::type_t::ref type_null();
types::type_t::ref type_void();
types::type_t::ref type_unreachable();
types::type_t::ref type_literal(token_t token);
types::type_t::ref type_integer(types::type_t::ref size, types::type_t::ref is_signed);
types::type_t::ref type_id(identifier::ref var);
types::type_t::ref type_variable(identifier::ref name);
types::type_t::ref type_variable(location_t location);
types::type_t::ref type_operator(types::type_t::ref operator_, types::type_t::ref operand);
types::type_module_t::ref type_module(types::type_t::ref module);
types::type_managed_t::ref type_managed(types::type_t::ref element);
types::type_struct_t::ref type_struct(types::type_t::refs dimensions, types::name_index_t name_index);
types::type_tuple_t::ref type_tuple(types::type_t::refs dimensions);
types::type_args_t::ref type_args(types::type_t::refs args, const identifier::refs &names={});
types::type_function_t::ref type_function(identifier::ref name, types::type_t::ref type_constraints, types::type_t::ref args, types::type_t::ref return_type);
types::type_t::ref type_and(types::type_t::refs terms);
types::type_t::ref type_sum(types::type_t::refs options, location_t location);
types::type_t::ref type_sum_safe(types::type_t::refs options, location_t location, const types::type_t::map &env);
types::type_t::ref type_maybe(types::type_t::ref just);
types::type_ptr_t::ref type_ptr(types::type_t::ref raw);
types::type_t::ref type_ref(types::type_t::ref raw);
types::type_t::ref type_lambda(identifier::ref binding, types::type_t::ref body);
types::type_t::ref type_extern(types::type_t::ref inner);

types::type_t::ref type_list_type(types::type_t::ref element);
types::type_t::ref type_vector_type(types::type_t::ref element);
types::type_t::ref type_strip_maybe(types::type_t::ref maybe_maybe);

types::type_t::ref full_eval(types::type_t::ref type, const types::type_t::map &env);
types::type_t::ref eval(types::type_t::ref type, const types::type_t::map &env);
types::type_t::ref eval_id(ptr<const types::type_id_t> ptid, const types::type_t::map &env);
types::type_t::ref eval_apply(types::type_t::ref oper, types::type_t::ref operand, const types::type_t::map &env);
bool type_is_unbound(types::type_t::ref type, const types::type_t::map &bindings);
std::ostream &operator <<(std::ostream &os, identifier::ref id);
std::string str(types::type_t::refs refs);
std::string str(types::type_t::map coll);
std::ostream& operator <<(std::ostream &out, const types::type_t::ref &type);

/* helper functions */
types::type_t::ref get_function_type_context(types::type_t::ref function_type);
types::type_t::ref get_function_return_type(types::type_t::ref function_type);
types::type_t::pair make_type_pair(std::string fst, std::string snd, identifier::set generics);

bool get_type_variable_name(types::type_t::ref type, std::string &name);
std::ostream &join_dimensions(std::ostream &os, const types::type_t::refs &dimensions, const types::name_index_t &name_index, const types::type_t::map &bindings);
std::string get_name_from_index(const types::name_index_t &name_index, int i);

extern const char *TYPE_OP_NOT;
extern const char *TYPE_OP_GC;
