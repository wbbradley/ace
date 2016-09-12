#include "zion.h"
#include "dbg.h"
#include "bound_type.h"
#include "scopes.h"
#include "bound_var.h"
#include "ast.h"
#include "type_instantiation.h"
#include "llvm_types.h"
#include "llvm_utils.h"

bound_type_impl_t::bound_type_impl_t(
		types::type::ref type,
		struct location location,
		llvm::Type *llvm_type,
		llvm::Type *llvm_specific_type,
		bound_type_t::refs dimensions,
		name_index member_index) :
	type(type),
	location(location),
	llvm_type(llvm_type),
	llvm_specific_type(llvm_specific_type),
	dimensions(dimensions),
	member_index(member_index)
{
	debug_above(6, log(log_info, "creating type with (%s, LLVM TypeID %d, %s, %s %s)",
			type->str().c_str(),
			llvm_type ? llvm_type->getTypeID() : -1,
			location.str().c_str(),
			::str(dimensions).c_str(),
			::str(member_index).c_str()));

	assert(llvm_type != nullptr);
}

types::type::ref bound_type_impl_t::get_type() const {
	return type;
}

struct location const bound_type_impl_t::get_location() const {
	return location;
}

llvm::Type * const bound_type_impl_t::get_llvm_type() const {
	return llvm_type;
}

llvm::Type * const bound_type_impl_t::get_llvm_specific_type() const {
	return llvm_specific_type;
}

bound_type_t::refs const bound_type_impl_t::get_dimensions() const {
	return dimensions;
}

bound_type_t::name_index const bound_type_impl_t::get_member_index() const {
	return member_index;
}

bound_type_t::ref bound_type_t::create(
		types::type::ref type,
		struct location location,
		llvm::Type *llvm_type,
		llvm::Type *llvm_specific_type,
		bound_type_t::refs dimensions,
		bound_type_t::name_index member_index)
{
	return make_ptr<bound_type_impl_t>(type, location, llvm_type,
			llvm_specific_type, dimensions, member_index);
}

bound_type_handle_t::ref bound_type_t::create_handle(
		types::type::ref type,
		llvm::Type *llvm_type)
{
	return make_ptr<bound_type_handle_t>(type, llvm_type);
}

bound_type_handle_t::bound_type_handle_t(
		types::type::ref type,
		llvm::Type *llvm_type) :
	type(type), llvm_type(llvm_type)
{
}

std::string bound_type_handle_t::str() const {
	if (actual != nullptr) {
		return actual->str();
	} else {
		std::stringstream ss;
		ss << get_type();
		ss << " " << llvm_print_type(*get_llvm_type());
		ss << " " C_UNCHECKED "unresolved" C_RESET;
		return ss.str();
	}
}

types::type::ref bound_type_handle_t::get_type() const {
	return type;
}

struct location const bound_type_handle_t::get_location() const {
	return type->get_location();
}

llvm::Type * const bound_type_handle_t::get_llvm_type() const {
	return llvm_type;
}

llvm::Type * const bound_type_handle_t::get_llvm_specific_type() const {
	if (actual != nullptr) {
		return actual->get_llvm_specific_type();
	} else {
		assert(false);
		return {};
	}
}

bound_type_t::refs const bound_type_handle_t::get_dimensions() const {
	if (actual != nullptr) {
		return actual->get_dimensions();
	} else {
		assert(false);
		return {};
	}
}

bound_type_t::name_index const bound_type_handle_t::get_member_index() const {
	if (actual != nullptr) {
		return actual->get_member_index();
	} else {
		assert(false);
		return {};
	}
}

void bound_type_handle_t::set_actual(bound_type_t::ref actual_) {
	assert(actual_ != actual);
	assert(actual_ != shared_from_this());
	assert(actual_->get_type()->str() == type->str());
	assert(actual_->get_llvm_type() == llvm_type);
	actual = actual_;
}

types::term::ref bound_type_t::get_term() const {
	return get_type()->to_term();
}

std::string str(const bound_type_t::refs &args) {
	std::stringstream ss;
	ss << "[";
	const char *sep = "";
	for (auto &arg : args) {
		ss << sep << arg->str();
		sep = ", ";
	}
	ss << "]";
	return ss.str();
}

std::string str(const bound_type_t::named_pairs &named_pairs) {
	std::stringstream ss;
	ss << "[";
	const char *sep = "";
	for (auto &pair : named_pairs) {
		ss << sep << "(" << pair.first << " " << pair.second->str() << ")";
		sep = ", ";
	}
	ss << "]";
	return ss.str();
}

std::string str(const bound_type_t::name_index &name_index) {
	std::stringstream ss;
	ss << "{";
	const char *sep = "";
	for (auto &pair : name_index) {
		ss << sep << pair.first << ": " << pair.second;
		sep = ", ";
	}
	ss << "}";
	return ss.str();
}

std::ostream &operator <<(std::ostream &os, const bound_type_t &type) {
	return os << type.str();
}

std::string bound_type_impl_t::str() const {
	std::stringstream ss;
	ss << get_type();
	ss << " " << llvm_print_type(*get_llvm_type());
	ss << " " << ::str(get_dimensions());
	return ss.str();
}

types::term::ref get_args_term(bound_type_t::named_pairs args) {
	types::term::refs sig_args;
	for (auto &named_pair : args) {
		sig_args.push_back(named_pair.second->get_type()->to_term());
	}
	return get_args_term(sig_args);
}

types::term::ref get_args_term(bound_type_t::refs args) {
	types::term::refs sig_args;
	for (auto &arg : args) {
		assert(arg != nullptr);
		sig_args.push_back(arg->get_type()->to_term());
	}
	return get_args_term(sig_args);
}

types::term::ref get_args_term(bound_var_t::refs args) {
	types::term::refs sig_args;
	for (auto &arg : args) {
		assert(arg != nullptr);
		sig_args.push_back(arg->get_term());
	}
	return get_args_term(sig_args);
}

types::term::refs get_terms(const bound_type_t::refs &bound_types) {
	types::term::refs terms;
	for (auto &bound_type : bound_types) {
		assert(bound_type != nullptr);
		terms.push_back(bound_type->get_term());
	}
	return terms;
}

types::term::ref get_tuple_term(const bound_type_t::refs &items_types) {
	types::term::refs dimensions;
	for (auto &arg : items_types) {
		assert(arg != nullptr);
		dimensions.push_back(arg->get_type()->to_term());
	}
	return get_tuple_term(dimensions);
}

types::term::ref get_tuple_term(types::term::refs dimensions) {
	return types::term_product(pk_tuple, dimensions);
}

bound_type_t::refs bound_type_t::refs_from_vars(const bound_var_t::refs &args) {
	bound_type_t::refs arg_types;
	for (auto &arg : args) {
		assert(arg != nullptr);
		assert(arg->type != nullptr);
		arg_types.push_back(arg->type);
	}
	return arg_types;
}

bool bound_type_t::is_function() const {
	return get_type()->is_function();
}

bool bound_type_t::is_void() const {
	return get_type()->is_void();
}

bool bound_type_t::is_obj() const {
	return get_type()->is_obj();
}

bool bound_type_t::is_struct() const {
	return get_type()->is_struct();
}

types::signature bound_type_t::get_signature() const {
	return get_type()->get_signature();
}

types::term::ref get_function_term(
		bound_type_t::refs args,
		bound_type_t::ref return_value)
{
	types::term::refs arg_terms;
	for (auto arg : args) {
		arg_terms.push_back(arg->get_type()->to_term());
	}
	types::term::ref args_term = get_args_term(arg_terms);
	return get_function_term(args_term, return_value->get_type()->to_term());
}

types::term::ref get_function_term(
		bound_type_t::named_pairs named_args,
		bound_type_t::ref ret)
{
	bound_type_t::refs args;
	for (auto named_arg : named_args) {
		args.push_back(named_arg.second);
	}
	return get_function_term(args, ret);
}

types::type::ref get_function_type(
		bound_type_t::refs args,
		bound_type_t::ref return_type)
{
	types::type::refs type_args;

	for (auto arg : args) {
		type_args.push_back(arg->get_type());
	}

	return ::type_product(pk_function,
			{::type_product(pk_args, type_args), return_type->get_type()});
}

namespace types {
	struct term_binder : public term {
		term_binder(
				llvm::IRBuilder<> &builder,
				scope_t::ref scope,
				identifier::ref id,
				ptr<ast::item const> node,
				types::term::ref data_ctor_sig,
				bound_type_t::name_index member_index) :
			builder(builder),
			scope(scope),
			id(id),
			node(node),
			data_ctor_sig(data_ctor_sig),
			member_index(member_index)
		{}
		virtual ~term_binder() {}

		llvm::IRBuilder<> &builder;
		scope_t::ref scope;
		identifier::ref const id;
		ptr<ast::item const> const node;
		types::term::ref const data_ctor_sig;
		bound_type_t::name_index const member_index;

		virtual std::ostream &emit(std::ostream &os) const {
			os << "(" << id;
			os << " " << data_ctor_sig;
			os << " {index: {";
			const char *sep = "";
			for (auto member_index_pair : member_index) {
				os << sep << member_index_pair.first;
				os << ": " << member_index_pair.second;
				sep = " ";
			}
			os << "}})";
			return os;
		}

		ref apply(ref operand) const {
			return types::term_binder(builder, scope, id, node,
					data_ctor_sig->apply(operand), member_index);
		}

		virtual ref evaluate(map env) const {
			return shared_from_this();
		}

		virtual type::ref get_type() const {
			auto program_scope = scope->get_program_scope();
			auto fn_type = data_ctor_sig->get_type();
			debug_above(5, log(log_info, "getting the type for %s",
						fn_type->str().c_str()));
			types::type::ref final_type = get_function_return_type(fn_type);
			types::type::refs data_ctor_args = get_function_type_args(fn_type);

			/* start by registering a placeholder handle for the data ctor's
			 * actual final type */
			auto bound_type_handle = bound_type_t::create_handle(
					final_type,
					program_scope->get_bound_type({"__var_ref"})->get_llvm_type());

#if 1
			std::stringstream ss;
			scope->dump(ss);
			debug_above(3, log(log_info, "looking for %s in %s",
					  final_type->get_signature().c_str(),
				  	  ss.str().c_str()));
#endif

			assert(scope->get_bound_type(final_type->get_signature()) == nullptr);
			program_scope->put_bound_type(bound_type_handle);

			debug_above(6, log(log_info, "this is " c_internal("crazy")
						" but we're going to push this type " c_type("%s") " into the scope now",
						str().c_str()));

			// TODO: plumb this status through get_type
			status_t status;
			bound_type_t::refs args;
			resolve_type_ref_params(status, builder, scope, data_ctor_args, args);

			if (!!status) {
				auto final_bound_type = create_algebraic_data_type(
						builder, scope, id, args, member_index, node,
						final_type);
				bound_type_handle->set_actual(final_bound_type);
				return final_type;
			}
			assert(false);
			return null_impl();
		}

		atom::set unbound_vars(atom::set bound_vars) const {
			not_impl();
			return atom::set{};
		}

		ref dequantify(atom::set generics) const {
			return null_impl();
		}
	};

	term::ref term_binder(
			llvm::IRBuilder<> &builder,
			scope_t::ref scope,
			identifier::ref id,
		   	ptr<ast::item const> node,
		   	types::term::ref data_ctor_sig,
		   	bound_type_t::name_index member_index)
   	{
		return make_ptr<struct term_binder>(builder, scope, id, node,
				data_ctor_sig, member_index);
	}
};
