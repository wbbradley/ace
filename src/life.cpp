#include "zion.h"
#include "life.h"
#include "callable.h"
#include <iostream>

life_form_t::life_form_t(int val) : val(val) {
}

const char *lfstr(life_form_t lf) {
	if (lf.is_statement()) {
		assert(!lf.is_loop());
		return "statement";
	} else if (lf.is_block()) {
		if (lf.is_loop()) {
			return "loop block";
		} else {
			return "block";
		}
	} else if (lf.is_function()) {
		assert(!lf.is_loop());
		return "function";
	} else if (lf.is_loop()) {
		return "loop";
	} else {
		assert(false);
		return "";
	}
}

life_t::life_t(
		status_t &status_tracker,
	   	life_form_t life_form,
	   	life_t::ref former_life) :
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
	debug_above(9, log("releasing vars from %s", lfstr(life_form_to_release_to)));
	debug_above(9, life_dump(shared_from_this()));

	exempt_life_release();

	if (!!status) {
		for (auto value: values) {
			assert(value->type->is_ref());
			llvm::AllocaInst *llvm_alloca = llvm::dyn_cast<llvm::AllocaInst>(value->get_llvm_value());
			/* be sure to null out any stack references as we pass out of scope so that the GC
			 * can avoid marking this guy */
			builder.CreateStore(
					llvm::Constant::getNullValue(llvm_deref_type(llvm_alloca->getType())),
					llvm_alloca);
		}

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

void life_t::track_var(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_var_t::ref value,
		life_form_t track_in_life_form)
{
	bool is_managed;
	value->type->is_managed_ptr(status, builder, scope, is_managed);

	if (!!status) {
		if (!is_managed) {
			/* we only track managed variables */
			debug_above(9, log("not tracking %s because it's not managed : %s",
						value->str().c_str(),
						value->type->str().c_str()));
			return;
		}

		// TODO: just track allocas for cleanup. avoid refcounting for now

		/* ensure there is a slot in the stack map for this heap pointer */
		value = llvm_stack_map_value(status, builder, scope, value);

		if (!!status) {
			if (this->life_form == track_in_life_form) {
				/* we track both LHS's and RHS's */
				values.push_back(value);
			} else {
				assert(this->former_life != nullptr && "We found a track_in_life_form for a life_form that is not on the stack.");
				this->former_life->track_var(status, builder, scope, value, track_in_life_form);
			}
			return;
		}
	}

	assert(!status);
}

std::string life_t::str() const {
	std::stringstream ss;
	ss << C_ID << "Life " << lfstr(life_form) << C_RESET << std::endl;
	for (auto value: values) {
		ss << value->str() << std::endl;
	}
	return ss.str();
}

void life_dump(ptr<const life_t> life) {
	std::stringstream ss;
	ss << C_CONTROL << "Life Dump:" << C_RESET << std::endl;
	while (life != nullptr) {
		ss << life->str() << std::endl;
		life = life->former_life;
	}
	log("%s", ss.str().c_str());
}
