#pragma once
#include "zion.h"
#include "env.h"
#include "ast_decls.h"
#include "utils.h"
#include "identifier.h"
#include "token.h"

extern const char *NULL_TYPE;
extern const char *STD_MANAGED_TYPE;
extern const char *STD_VECTOR_TYPE;
extern const char *STD_MAP_TYPE;
extern const char *VOID_TYPE;
extern const char *BOTTOM_TYPE;

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


extern const char *TYPE_OP_NOT;
extern const char *TYPE_OP_IF;
extern const char *TYPE_OP_GC;
extern const char *TYPE_OP_IS_REF;
extern const char *TYPE_OP_IS_TRUE;
extern const char *TYPE_OP_IS_FALSE;
extern const char *TYPE_OP_IS_BOOL;
extern const char *TYPE_OP_IS_STR;
extern const char *TYPE_OP_IS_POINTER;
extern const char *TYPE_OP_IS_FUNCTION;
extern const char *TYPE_OP_IS_CALLABLE;
extern const char *TYPE_OP_IS_VOID;
extern const char *TYPE_OP_IS_UNIT;
extern const char *TYPE_OP_IS_NULL;
extern const char *TYPE_OP_IS_INT;
extern const char *TYPE_OP_IS_MAYBE;

enum type_builtins_t {
	tb_gc,
	tb_ref,
	tb_true,
	tb_false,
	tb_bool,
	tb_pointer,
	tb_function,
	tb_callable,
	tb_void,
	tb_unit,
	tb_int,
	tb_str,
	tb_null,
	tb_maybe,
};

namespace types {

	extern env_t::ref _empty_env;
	typedef std::map<std::string, int> name_index_t;

	struct signature;

	struct type_t : public std::enable_shared_from_this<type_t> {
		typedef ptr<const type_t> ref;
		typedef std::vector<ref> refs;
		typedef std::map<std::string, ref> map;
        typedef std::pair<ref, ref> pair;

		virtual ~type_t() {}
		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const = 0;

		/* how many free type variables exist in this type? NB: Assumes you have
         * already bound existing bindings at the callsite prior to this check. */
		virtual int ftv_count() const = 0;

        /* NB: Also assumes you have rebound the bindings at the callsite. */
		virtual std::set<std::string> get_ftvs() const = 0;

		std::string repr(const map &bindings) const;
		std::string repr() const { return this->repr({}); }

		virtual location_t get_location() const = 0;

		std::string str() const;
		std::string str(const map &bindings) const;
		std::string get_signature() const { return repr(); }

		virtual ref rebind(const map &bindings, bool bottom_out_free_vars=false) const = 0;
		ref eval(env_t::ref env, bool get_structural_type=false) const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const = 0;
        virtual type_t::ref boolean_refinement(bool elimination_value, env_t::ref env) const;
		virtual void encode(env_t::ref env, std::vector<uint16_t> &encoding) const;

		/* helpers */
		bool eval_predicate(type_builtins_t tb, env_t::ref env) const;

		virtual int get_precedence() const { return 10; }
	};

	struct type_subtype_t : public type_t {
		type_subtype_t(const type_t::ref lhs, const type_t::ref rhs);
		const type_t::ref lhs;
		const type_t::ref rhs;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual int get_precedence() const { return 6; }
		virtual ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const;
	};

	struct type_product_t : public type_t {
		typedef ptr<const type_product_t> ref;

		virtual product_kind_t get_pk() const = 0;
		virtual type_t::refs get_dimensions() const = 0;
	};

	struct type_args_t : public type_product_t {
		typedef ptr<const type_args_t> ref;

		type_args_t(type_t::refs args, identifier::refs names);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const;

		type_t::refs args;
		identifier::refs names;
	};

	struct type_variable_t : public type_t {
		type_variable_t(identifier::ref id);
		type_variable_t(location_t location /* auto-generated fresh type variables */);
		identifier::ref id;
		location_t location;

		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const { return shared_from_this(); }
		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
	};

	struct type_data_t : public type_t {
		type_data_t(token_t name, type_variable_t::refs type_vars, std::vector<std::pair<token_t, types::type_args_t::ref>> ctor_pairs);

		token_t name;
		type_t::refs type_vars;
		std::vector<std::pair<token_t, types::type_args_t::ref>> ctor_pairs;

		typedef ptr<const type_data_t> ref;

		virtual int get_precedence() const { return 3; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref boolean_refinement(bool elimination_value, env_t::ref env) const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_env) const;
		virtual void encode(env_t::ref env, std::vector<uint16_t> &encoding) const;
	};

	bool is_type_id(type_t::ref type, const std::string &type_name, env_t::ref env);
	bool is_ptr_type_id(type_t::ref type, const std::string &type_name, env_t::ref env, bool allow_maybe=false);
	bool is_managed_ptr(types::type_t::ref type, env_t::ref env);
	bool is_ptr(types::type_t::ref type, env_t::ref env);

	struct type_id_t : public type_t {
		type_id_t(identifier::ref id);
		identifier::ref id;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
        virtual type_t::ref boolean_refinement(bool elimination_value, env_t::ref env) const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_env) const;
		virtual void encode(env_t::ref env, std::vector<uint16_t> &encoding) const;
	};

	struct type_operator_t : public type_t {
		typedef ptr<const type_operator_t> ref;

		type_operator_t(type_t::ref oper, type_t::ref operand);
		type_t::ref oper;
		type_t::ref operand;

		virtual int get_precedence() const { return 7; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
        virtual type_t::ref boolean_refinement(bool elimination_value, env_t::ref env) const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_env) const;
		virtual void encode(env_t::ref env, std::vector<uint16_t> &encoding) const;
	};

	struct type_any_of_t : public type_t {
		typedef ptr<const type_any_of_t> ref;
		
		type_any_of_t(const map &shapes);
		refs shapes;

		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const { return shared_from_this(); }
		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
	};

	struct type_literal_t : public type_t {
		type_literal_t(token_t token);
		token_t token;

		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const { return shared_from_this(); }
		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		int coerce_to_int() const;
	};

	struct type_integer_t : public type_t {
		typedef ptr<const type_integer_t> ref;
		type_integer_t(type_t::ref bit_size, type_t::ref signed_);
		type_t::ref bit_size;
		type_t::ref signed_;

		virtual int get_precedence() const { return 9; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
        virtual type_t::ref boolean_refinement(bool elimination_value, env_t::ref env) const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const;
	};

	struct type_injection_t : public type_product_t {
		typedef ptr<const type_injection_t> ref;

		virtual int get_precedence() const { return 0; }

		type_injection_t(type_t::ref module_type);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const;

		type_t::ref module_type;
	};

	struct type_managed_t : public type_product_t {
		typedef ptr<const type_managed_t> ref;

		type_managed_t(type_t::ref element_type);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const;

		type_t::ref element_type;
	};

	struct type_struct_t : public type_product_t {
		typedef ptr<const type_struct_t> ref;

		type_struct_t(type_t::refs dimensions, name_index_t name_index);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const;

		type_t::refs dimensions;
		name_index_t name_index;
	};

	struct type_tuple_t : public type_product_t {
		typedef ptr<const type_tuple_t> ref;

		type_tuple_t(type_t::refs dimensions);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const;

		type_t::refs dimensions;
	};

	struct type_function_t : public type_t {
		typedef ptr<const type_function_t> ref;
		type_function_t(
				location_t location,
			   	types::type_t::ref type_constraints,
				types::type_t::ref args,
			   	type_t::ref return_type);

		location_t location;
		type_t::ref type_constraints;
		type_t::ref args;
		type_t::ref return_type;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const;
	};

	struct type_function_closure_t : public type_t {
		typedef ptr<const type_function_closure_t> ref;
		type_function_closure_t(types::type_t::ref function);

		type_t::ref function;
		/* the type of the captured variables object aka "closure" is opaque, therefore it is
		 * unnecessary to be represented in the type system */

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const;
	};

	struct type_eq_t : public type_t {
		type_eq_t(type_t::ref lhs, type_t::ref rhs, location_t location);
		type_t::ref lhs, rhs;
		location_t location;

		static const token_kind TK;

		virtual int get_precedence() const { return 5; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_env) const;
	};

	struct type_and_t : public type_t {
		type_and_t(type_t::refs terms);
		type_t::refs terms;
		location_t location;

		virtual int get_precedence() const { return 4; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_env) const;
	};

	struct type_maybe_t : public type_t {
		type_maybe_t(type_t::ref just);
		type_t::ref just;

		virtual int get_precedence() const { return 8; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
        virtual type_t::ref boolean_refinement(bool elimination_value, env_t::ref env) const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_env) const;
	};

	struct type_ptr_t : public type_t {
		typedef ptr<const type_ptr_t> ref;
		type_ptr_t(type_t::ref raw);
		type_t::ref element_type;

		virtual int get_precedence() const { return 10; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref boolean_refinement(bool elimination_value, env_t::ref env) const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_env) const;
	};

	struct type_ref_t : public type_t {
		typedef ptr<const type_ref_t> ref;
		type_ref_t(type_t::ref raw);
		type_t::ref element_type;

		virtual int get_precedence() const { return 10; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;

		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_env) const;
	};

	struct type_lambda_t : public type_t {
		type_lambda_t(identifier::ref binding, type_t::ref body);
		identifier::ref binding;
		type_t::ref body;

		virtual int get_precedence() const { return 6; }
		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const;
	};

	struct type_extern_t : public type_t {
		typedef ptr<const type_extern_t> ref;
		type_extern_t(type_t::ref inner);
		type_t::ref inner;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &bindings, bool bottom_out_free_vars=false) const;
		virtual location_t get_location() const;
		virtual type_t::ref eval_core(env_t::ref env, bool get_structural_type) const;
	};


	identifier::ref gensym(location_t location);
	int coerce_to_integer(env_t::ref env, type_t::ref type, type_t::ref &expansion);
	bool is_integer(type_t::ref type, env_t::ref env);
	bool maybe_get_integer_attributes(
			location_t location,
		   	type_t::ref type,
		   	env_t::ref env,
		   	unsigned &bit_size,
		   	bool &signed_);
	void get_integer_attributes(
			location_t location,
			type_integer_t::ref type,
			env_t::ref env,
			unsigned &bit_size,
			bool &signed_);
	void get_runtime_typeids(type_t::ref type, env_t::ref env, std::set<int> &typeids);
	type_t::ref without_ref(type_t::ref type);
	type_t::refs without_refs(type_t::refs types);
	type_function_t::ref without_closure(type_t::ref type);
	types::type_t::ref freshen(types::type_t::ref type);
	bool share_ftvs(types::type_t::ref lhs, types::type_t::ref rhs);
};

/* type data ctors */
types::type_t::ref type_bottom();
types::type_t::ref type_unit();
types::type_t::ref type_null();
types::type_t::ref type_void();
types::type_t::ref type_literal(token_t token);
types::type_t::ref type_integer(types::type_t::ref size, types::type_t::ref is_signed);
types::type_t::ref type_id(identifier::ref var);
types::type_t::ref type_variable(identifier::ref name);
types::type_t::ref type_variable(location_t location);
types::type_t::ref type_operator(types::type_t::ref operator_, types::type_t::ref operand);
types::type_t::ref type_subtype(types::type_t::ref lhs, types::type_t::ref rhs);
types::type_injection_t::ref type_injection(types::type_t::ref module);
types::type_managed_t::ref type_managed(types::type_t::ref element);
types::type_struct_t::ref type_struct(types::type_t::refs dimensions, types::name_index_t name_index);
types::type_struct_t::ref type_struct(types::type_args_t::ref type_args);
types::type_tuple_t::ref type_tuple(types::type_t::refs dimensions);
types::type_args_t::ref type_args(types::type_t::refs args, const identifier::refs &names={});
types::type_function_t::ref type_function(location_t location, types::type_t::ref type_constraints, types::type_t::ref args, types::type_t::ref return_type);
types::type_function_closure_t::ref type_function_closure(types::type_t::ref function);
types::type_t::ref get_arg_from_function(types::type_function_t::ref function, size_t i);
types::type_t::ref type_and(types::type_t::refs terms);
types::type_t::ref type_eq(types::type_t::ref lhs, types::type_t::ref rhs, location_t location);
types::type_t::ref type_data(token_t name, types::type_variable_t::refs type_vars, std::vector<std::pair<token_t, types::type_args_t::ref>> ctor_pairs);
types::type_t::ref type_maybe(types::type_t::ref just, env_t::ref env);
types::type_ptr_t::ref type_ptr(types::type_t::ref raw);
types::type_t::ref type_ref(types::type_t::ref raw);
types::type_t::ref type_lambda(identifier::ref binding, types::type_t::ref body);
types::type_t::ref type_extern(types::type_t::ref inner);
types::type_function_t::ref type_deferred_function(location_t location, types::type_t::ref return_type);

types::type_t::ref type_list_type(types::type_t::ref element);
types::type_t::ref type_vector_type(types::type_t::ref element);
types::type_t::ref type_strip_maybe(types::type_t::ref maybe_maybe);

std::ostream &operator <<(std::ostream &os, identifier::ref id);
std::string str(types::type_t::refs refs);
std::string str(const types::type_t::map &coll);
std::ostream& operator <<(std::ostream &out, const types::type_t::ref &type);

/* helper functions */
types::type_t::ref get_function_type_context(types::type_t::ref function_type);
types::type_t::ref get_function_return_type(types::type_t::ref function_type);
types::type_t::pair make_type_pair(std::string fst, std::string snd, identifier::set generics);

bool get_type_variable_name(types::type_t::ref type, std::string &name);
std::ostream &join_dimensions(std::ostream &os, const types::type_t::refs &dimensions, const types::name_index_t &name_index, const types::type_t::map &bindings);
std::string get_name_from_index(const types::name_index_t &name_index, int i);
bool is_valid_udt_initial_char(int ch);
