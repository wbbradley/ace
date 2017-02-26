#include "zion.h"
#include "life.h"
#include "callable.h"

life_t::life_t(life_form_t life_form, life_t::ref former_life) :
	former_life(former_life), life_form(life_form), release_vars_called(false)
{
	/* we don't have support for closures yet */
	assert(bool(former_life != nullptr) ^ bool(life_form == lf_function));
}

life_t::~life_t() {
	assert(((values.size() == 0) ^ release_vars_called) && "We've cleaned up the bound vars");
}

life_t::ref life_t::new_life(life_form_t life_form) {
	return make_ptr<life_t>(life_form, shared_from_this());
}

void life_t::release_vars(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_form_t life_form) const
{
	release_vars_called = values.size() != 0;

	if (!!status) {
		auto program_scope = scope->get_program_scope();
		auto release_function = program_scope->get_singleton({"__release_var"});

		/* this is just a placeholder life to capture any spurious output from the
		 * release call. none should exist */
		life_t::ref life = make_ptr<life_t>(lf_function, nullptr);
		for (auto value: values) {
			debug_above(4, log("releasing var %s", value->str().c_str()));

			if (!!status) {
				make_call_value(
						status,
						builder,
						INTERNAL_LOC(),
						scope,
						life,
						release_function,
						{value});
			}
		}
	}
}

void life_t::track_var(
		llvm::IRBuilder<> &builder,
	   	bound_var_t::ref value,
	   	life_form_t track_in_life_form)
{
	/* we only track managed variables */
	if (this->life_form == track_in_life_form) {
		/* first check if this value is an alloca, if it is, then we need to store
		 * its current value so we can release it later. if it's an rhs, then we can
		 * just stash it */
		if (llvm::AllocaInst *llvm_alloca = llvm::dyn_cast<llvm::AllocaInst>(value->llvm_value)) {
			assert(value->is_lhs);
			values.push_back(
					bound_var_t::create(
						INTERNAL_LOC(),
						string_format("__saved_for_release_%s", value->name.c_str()),
						value->type,
						llvm_resolve_alloca(builder, llvm_alloca),
						value->id,
						false /*is_lhs*/));
		} else {
			assert(!value->is_lhs);
			not_impl();
		}
	} else {
		assert(this->former_life != nullptr && "We found a track_in_life_form for a life_form that is not on the stack.");
		this->former_life->track_var(builder, value, track_in_life_form);
	}
}

void call_addref_var(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
		scope_t::ref scope,
	   	bound_var_t::ref var)
{
	auto program_scope = scope->get_program_scope();
	auto addref_function = program_scope->get_singleton({"__addref_var"});
	life_t::ref life = make_ptr<life_t>(lf_function, nullptr);
	make_call_value(
			status,
			builder,
			INTERNAL_LOC(),
			scope,
			life,
			addref_function,
			{var});
}
