#include "zion.h"
#include "life.h"
#include "callable.h"
#include "unification.h"
#include <iostream>


struct bound_var_trackable_t : public trackable_t {
	bound_var_trackable_t(bound_var_t::ref value) : value(value) {
	}

	const bound_var_t::ref value;
	
	std::string str() const override {
		return value->str();
	}

	virtual location_t get_location() const override {
		return value->get_location();
	}

	virtual void release(llvm::IRBuilder<> &builder, scope_t::ref scope) const override {
		assert(value->type->is_ref(scope));
		llvm::AllocaInst *llvm_alloca = llvm::dyn_cast<llvm::AllocaInst>(value->get_llvm_value());
		/* be sure to null out any stack references as we pass out of scope so that the GC
		 * can avoid marking this guy */
		builder.CreateStore(
				llvm::Constant::getNullValue(llvm_deref_type(llvm_alloca->getType())),
				llvm_alloca);
	}
};

struct defer_call_trackable_t : public trackable_t {
	defer_call_trackable_t(bound_var_t::ref callable) : callable(callable) {
	}

	const bound_var_t::ref callable;
	
	std::string str() const override {
		return callable->str();
	}

	virtual location_t get_location() const override {
		return callable->get_location();
	}

	virtual void release(llvm::IRBuilder<> &builder, scope_t::ref scope) const override {
		assert(!callable->type->is_ref(scope));
		assert(callable->type->get_type()->eval_predicate(tb_callable, scope));

		auto fake_life = (
				make_ptr<life_t>(lf_function)
				->new_life(lf_block)
				->new_life(lf_statement));

		create_callsite(
				builder,
				scope,
				fake_life,
				callable,
				"cleanup.call",
				get_location(),
				{});
	}
};

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
	   	life_form_t life_form,
	   	life_t::ref former_life) :
   	former_life(former_life),
   	life_form(life_form),
   	release_vars_called(false)
{
}

life_t::~life_t() {
	if (!std::uncaught_exception()) {
		/* only do this check if something didn't go wrong, otherwise we'll get a noisy error */
		if (((values.size() == 0) ^ release_vars_called) && "We've cleaned up the bound vars") {
        } else {
            for (auto &value : values) {
                log_location(log_error, value->get_location(), "unfreed variable %s", value->str().c_str());
            }
            assert(false);
        }
	}
}

life_t::ref life_t::new_life(life_form_t life_form) {
	return make_ptr<life_t>(life_form, shared_from_this());
}

void life_t::exempt_life_release() const {
	release_vars_called = values.size() != 0;
}

int life_t::release_vars(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		life_form_t life_form_to_release_to) const
{
	int released_count = 0;
	debug_above(9, log("releasing vars from %s", lfstr(life_form_to_release_to)));
	debug_above(9, life_dump(shared_from_this()));

	exempt_life_release();

	for (auto &value: values) {
		value->release(builder, scope);
		++released_count;
	}

	if (life_form_to_release_to != life_form) {
		if (former_life != nullptr) {
			/* recurse into former lives */
			released_count += former_life->release_vars(builder, scope,
					life_form_to_release_to);
		} else {
			assert(false && "We can't release to the requested life form because it doesn't exist!");
		}
	}
	return released_count;
}

void life_t::defer_call(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_var_t::ref callable)
{
	if (this->life_form == lf_block) {
		if (!unifies(
					callable->type->get_type(),
					type_deferred_function(callable->get_location(), type_void()),
					scope) &&
				!unifies(
					callable->type->get_type(),
					type_deferred_function(callable->get_location(), type_unit()),
					scope))
		{
			auto error = user_error(callable->get_location(), "deferred expression must be a function taking no arguments, and returning void or ()");
			error.add_info(callable->get_location(), "the type of the deferred expression given is %s",
					callable->type->get_type()->str().c_str());
			throw error;
		}

		/* ensure this callable doesn't go away before its used. */
		values.push_front(std::make_unique<bound_var_trackable_t>(llvm_stack_map_value(builder, scope, callable)));

		values.push_front(std::make_unique<defer_call_trackable_t>(callable));
	} else {
		assert(this->former_life != nullptr && "We found a track_in_life_form for a life_form that is not on the stack.");
		this->former_life->defer_call(builder, scope, callable);
	}
}

void life_t::track_var(
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_var_t::ref value,
		life_form_t track_in_life_form)
{
	if (this->life_form == track_in_life_form) {
		bool is_managed;
		value->type->is_managed_ptr(builder, scope, is_managed);

		if (!is_managed) {
			/* we only track managed variables */
			debug_above(9, log("not tracking %s because it's not managed : %s",
						value->str().c_str(),
						value->type->str().c_str()));
			return;
		}

		if (value->type->get_type()->eval_predicate(tb_unit, scope)) {
			debug_above(9, log("not tracking %s because it is the unit type", value->str().c_str()));
			return;
		}

		/* ensure there is a slot in the stack map for this heap pointer */
		value = llvm_stack_map_value(builder, scope, value);

		values.push_back(std::make_unique<bound_var_trackable_t>(value));
	} else {
		assert(this->former_life != nullptr && "We found a track_in_life_form for a life_form that is not on the stack.");
		this->former_life->track_var(builder, scope, value, track_in_life_form);
	}
}

std::string life_t::str() const {
	std::stringstream ss;
	ss << C_ID << "Life " << lfstr(life_form) << C_RESET << std::endl;
	for (auto &value: values) {
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

