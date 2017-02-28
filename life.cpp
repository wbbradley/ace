#include "zion.h"
#include "life.h"
#include "callable.h"
#include <iostream>

const char *lfstr(life_form_t lf) {
	switch (lf) {
	case lf_function:
		return "function";
	case lf_block:
		return "block";
	case lf_statement:
		return "statement";
	case lf_loop:
		return "loop";
	}
}

life_t::life_t(status_t &status_tracker, life_form_t life_form, life_t::ref former_life) :
	status_tracker(status_tracker),
   	former_life(former_life),
   	life_form(life_form),
   	release_vars_called(false)
{
}

life_t::~life_t() {
	if (!!status_tracker) {
		assert(((values.size() == 0) ^ release_vars_called) && "We've cleaned up the bound vars");
	}
}

life_t::ref life_t::new_life(status_t &status_tracker, life_form_t life_form) {
	return make_ptr<life_t>(status_tracker, life_form, shared_from_this());
}

void life_t::exempt_life_release() const {
	release_vars_called = values.size() != 0;
}

void life_t::release_vars(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_form_t life_form_to_release_to) const
{
	debug_above(4, log("releasing vars from %s", lfstr(life_form_to_release_to)));
	life_dump(shared_from_this());

	exempt_life_release();

	if (!!status) {
		auto program_scope = scope->get_program_scope();
		auto release_function = program_scope->get_singleton({"__release_var"});

		/* this is just a placeholder life to capture any spurious output from the
		 * release call. none should exist */
		life_t::ref life = make_ptr<life_t>(status, lf_statement, nullptr);
		for (auto value: values) {
			debug_above(4, log("releasing var %s", value->str().c_str()));

			make_call_value(
					status,
					builder,
					INTERNAL_LOC(),
					scope,
					life,
					release_function,
					{value});

			if (!status) {
				break;
			}
		}

		if (!!status) {
			if (life_form_to_release_to != life_form) {
				if (former_life != nullptr) {
					/* recurse into former lives */
					former_life->release_vars(status, builder, scope,
							life_form_to_release_to);
				} else {
					assert(false && "We can't release to the requested life form because it doesn't exist!");
				}
			}
		}
	}
}

void life_t::track_var(
		llvm::IRBuilder<> &builder,
	   	bound_var_t::ref value,
	   	life_form_t track_in_life_form)
{
	assert(life_form != lf_loop);
	if (!value->type->is_managed()) {
		debug_above(6, log("not tracking %s because it's not managed : %s",
					value->str().c_str(),
					value->type->str().c_str()));
		return;
	}

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
			values.push_back(value);
		}
	} else {
		assert(this->former_life != nullptr && "We found a track_in_life_form for a life_form that is not on the stack.");
		this->former_life->track_var(builder, value, track_in_life_form);
	}
}

std::string life_t::str() const {
	std::stringstream ss;
	ss << C_ID << "Life " << lfstr(life_form) << C_RESET << std::endl;
	for (auto value: values) {
		ss << value->str() << std::endl;
	}
	return ss.str();
}

void call_addref_var(
		status_t &status,
	   	llvm::IRBuilder<> &builder,
		scope_t::ref scope,
	   	bound_var_t::ref var)
{
	if (!!status) {
		assert(var != nullptr);

		if (var->type->is_managed()) {
			auto program_scope = scope->get_program_scope();
			auto addref_function = program_scope->get_singleton({"__addref_var"});
			life_t::ref life = make_ptr<life_t>(status, lf_statement, nullptr);
			make_call_value(
					status,
					builder,
					INTERNAL_LOC(),
					scope,
					life,
					addref_function,
					{var});
			/* since addref_var returns a void, we can discard this life as it
			 * won't have anything to clean up. */
		}
	}
}

void life_dump(ptr<const life_t> life) {
	std::stringstream ss;
	ss << C_CONTROL << "Life Dump:" << C_RESET << std::endl;
	while (life != nullptr) {
		ss << life->str() << std::endl;
		life = life->former_life;
	}
	debug_above(3, log("%s", ss.str().c_str()));
}
