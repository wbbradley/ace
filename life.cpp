#include "zion.h"
#include "life.h"

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
	auto program_scope = scope->get_program_scope();
	for (auto value: values) {
		debug_above(4, log("releasing var %s", value->str().c_str()));
		auto release_function = program_scope->get_singleton({"__release_var"});

		if (!!status) {
			release_vars_called = true;
			// TODO: call the release
			not_impl();
		}
	}
}
