#pragma once
#include "ptr.h"
#include "bound_var.h"
#include "status.h"
#include "llvm_utils.h"

/* life_form_t is another word for scope or extent. In Zion, scope and extent
 * should be equivalent although there are unnamed values that need treatment at
 * the statement level. these may not have names and thusly are not mentioned in
 * scopes. */
enum life_form_t {
	lf_statement=0,
	lf_block,
	lf_loop,
	lf_function,
};

struct life_t : std::enable_shared_from_this<life_t> {
	typedef ptr<life_t> ref;

	life_t(life_form_t life_form, life_t::ref former_life=nullptr);
	~life_t();

	life_t(const life_t &life) = delete;
	life_t &operator =(const life_t &life) = delete;

	void track_var(bound_var_t::ref value);
	void release_vars(status_t &status, llvm::IRBuilder<> &builder, scope_t::ref scope, life_form_t life_form) const;
	life_t::ref new_life(life_form_t life_form);

	const life_t::ref former_life;
	const life_form_t life_form;
	std::vector<bound_var_t::ref> values;
	mutable bool release_vars_called;
};
