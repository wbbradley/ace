#pragma once
#include "zion.h"
#include "ast_decls.h"
#include "utils.h"
#include "identifier.h"
#include "token.h"

struct env_t;

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

using env_ref_t = const env_t &;

namespace types {
	typedef std::map<std::string, int> name_index_t;

	struct signature;
	struct forall_t;

	struct type_t : public std::enable_shared_from_this<type_t> {
		typedef std::shared_ptr<const type_t> ref;
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

		std::shared_ptr<forall_t> generalize(env_ref_t env) const;
		std::string repr(const map &bindings) const;
		std::string repr() const { return this->repr({}); }

		virtual location_t get_location() const = 0;

		std::string str() const;
		std::string str(const map &bindings) const;
		std::string get_signature() const { return repr(); }

		virtual type_t::ref rebind(const map &env) const = 0;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const = 0;

		virtual int get_precedence() const { return 10; }
	};

	struct type_product_t : public type_t {
		typedef std::shared_ptr<const type_product_t> ref;

		virtual product_kind_t get_pk() const = 0;
		virtual type_t::refs get_dimensions() const = 0;
	};

	struct type_args_t : public type_product_t {
		typedef std::shared_ptr<const type_args_t> ref;

		type_args_t(type_t::refs args, identifiers_t names);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &env) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual location_t get_location() const;

		type_t::refs args;
		identifiers_t names;
	};

	struct type_variable_t : public type_t {
		type_variable_t(identifier_t id);
		type_variable_t(location_t location /* auto-generated fresh type variables */);
		identifier_t id;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &env) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual location_t get_location() const;
	};

	struct type_data_t : public type_t {
		type_data_t(token_t name, type_variable_t::refs type_vars, std::vector<std::pair<token_t, types::type_args_t::ref>> ctor_pairs);

		token_t name;
		type_t::refs type_vars;
		std::vector<std::pair<token_t, types::type_args_t::ref>> ctor_pairs;

		typedef std::shared_ptr<const type_data_t> ref;

		virtual int get_precedence() const { return 3; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &env) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual location_t get_location() const;
	};

	struct type_id_t : public type_t {
		type_id_t(identifier_t id);
		identifier_t id;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &env) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual location_t get_location() const;
	};

	struct type_operator_t : public type_t {
		typedef std::shared_ptr<const type_operator_t> ref;

		type_operator_t(type_t::ref oper, type_t::ref operand);
		type_t::ref oper;
		type_t::ref operand;

		virtual int get_precedence() const { return 7; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &env) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual location_t get_location() const;
	};

	struct type_any_of_t : public type_t {
		typedef std::shared_ptr<const type_any_of_t> ref;
		
		type_any_of_t(const map &shapes);
		refs shapes;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &env) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual location_t get_location() const;
	};

	struct type_integer_t : public type_t {
		typedef std::shared_ptr<const type_integer_t> ref;
		type_integer_t(type_t::ref bit_size, type_t::ref signed_);
		type_t::ref bit_size;
		type_t::ref signed_;

		virtual int get_precedence() const { return 9; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &env) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual location_t get_location() const;
	};

	struct type_struct_t : public type_product_t {
		typedef std::shared_ptr<const type_struct_t> ref;

		type_struct_t(type_t::refs dimensions, name_index_t name_index);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &env) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual location_t get_location() const;

		type_t::refs dimensions;
		name_index_t name_index;
	};

	struct type_tuple_t : public type_product_t {
		typedef std::shared_ptr<const type_tuple_t> ref;

		type_tuple_t(type_t::refs dimensions);

		virtual product_kind_t get_pk() const;
		virtual type_t::refs get_dimensions() const;

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &env) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual location_t get_location() const;

		type_t::refs dimensions;
	};

	struct type_ptr_t : public type_t {
		typedef std::shared_ptr<const type_ptr_t> ref;
		type_ptr_t(type_t::ref raw);
		type_t::ref element_type;

		virtual int get_precedence() const { return 10; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &env) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual location_t get_location() const;
	};

	struct type_ref_t : public type_t {
		typedef std::shared_ptr<const type_ref_t> ref;
		type_ref_t(type_t::ref raw);
		type_t::ref element_type;

		virtual int get_precedence() const { return 10; }

		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &env) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual location_t get_location() const;

	};

	struct type_lambda_t : public type_t {
		type_lambda_t(identifier_t binding, type_t::ref body);
		identifier_t binding;
		type_t::ref body;

		virtual int get_precedence() const { return 6; }
		virtual std::ostream &emit(std::ostream &os, const map &bindings, int parent_precedence) const;
		virtual int ftv_count() const;
		virtual std::set<std::string> get_ftvs() const;
		virtual type_t::ref rebind(const map &env) const;
		virtual type_t::ref remap_vars(const std::map<std::string, std::string> &map) const;
		virtual location_t get_location() const;
	};

	struct forall_t {
		typedef std::shared_ptr<forall_t> ref;
		forall_t(std::vector<std::string> vars, types::type_t::ref type) : vars(vars), type(type) {}
		types::type_t::ref instantiate(location_t location);
		forall_t::ref rebind(const types::type_t::map &env);
		forall_t::ref normalize();
		std::set<std::string> get_ftvs();
		std::string str();

		std::vector<std::string> vars;
		types::type_t::ref type;
	};

	identifier_t gensym(location_t location);
	int coerce_to_integer(env_ref_t env, type_t::ref type, type_t::ref &expansion);
	type_t::ref without_ref(type_t::ref type);
	type_t::refs without_refs(type_t::refs types);
	types::type_t::ref freshen(types::type_t::ref type);
	bool share_ftvs(types::type_t::ref lhs, types::type_t::ref rhs);
	bool is_type_id(type_t::ref type, const std::string &type_name, env_ref_t env);
};

/* type data ctors */
types::type_t::ref type_bottom();
types::type_t::ref type_bool(location_t location);
types::type_t::ref type_string(location_t location);
types::type_t::ref type_int(location_t location);
types::type_t::ref type_unit(location_t location);
types::type_t::ref type_null(location_t location);
types::type_t::ref type_void(location_t location);
types::type_t::ref type_arrow(location_t location, types::type_t::ref a, types::type_t::ref b);
types::type_t::ref type_integer(types::type_t::ref size, types::type_t::ref is_signed);
types::type_t::ref type_id(identifier_t var);
types::type_t::ref type_variable(identifier_t name);
types::type_t::ref type_variable(location_t location);
types::type_t::ref type_operator(types::type_t::ref operator_, types::type_t::ref operand);
types::forall_t::ref forall(std::vector<std::string> vars, types::type_t::ref type);
types::type_struct_t::ref type_struct(types::type_t::refs dimensions, types::name_index_t name_index);
types::type_struct_t::ref type_struct(types::type_args_t::ref type_args);
types::type_tuple_t::ref type_tuple(types::type_t::refs dimensions);
types::type_args_t::ref type_args(types::type_t::refs args, const identifiers_t &names={});
types::type_t::ref type_data(token_t name, types::type_variable_t::refs type_vars, std::vector<std::pair<token_t, types::type_args_t::ref>> ctor_pairs);
types::type_t::ref type_maybe(types::type_t::ref just, env_ref_t env);
types::type_ptr_t::ref type_ptr(types::type_t::ref raw);
types::type_t::ref type_ref(types::type_t::ref raw);
types::type_t::ref type_lambda(identifier_t binding, types::type_t::ref body);

types::type_t::ref type_vector_type(types::type_t::ref element);

std::string str(types::type_t::refs refs);
std::string str(const types::type_t::map &coll);
std::ostream& operator <<(std::ostream &out, const types::type_t::ref &type);

bool get_type_variable_name(types::type_t::ref type, std::string &name);
std::ostream &join_dimensions(std::ostream &os, const types::type_t::refs &dimensions, const types::name_index_t &name_index, const types::type_t::map &bindings);
std::string get_name_from_index(const types::name_index_t &name_index, int i);
bool is_valid_udt_initial_char(int ch);
