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
	lf_function=0,
	lf_block,
	lf_loop,
	lf_statement,
};

struct life_t : std::enable_shared_from_this<life_t> {
	typedef ptr<life_t> ref;

	life_t(status_t &status_tracker, life_form_t life_form, life_t::ref former_life=nullptr);
	~life_t();

	life_t(const life_t &life) = delete;
	life_t &operator =(const life_t &life) = delete;

	std::string str() const;

	/* track a value for later release at a given life_form level */
	void track_var(
			status_t &status,
			llvm::IRBuilder<> &builder,
			scope_t::ref scope,
			bound_var_t::ref value,
			life_form_t track_in_life_form);

	/* release values down to and including a particular life_form level */
	void release_vars(
			status_t &status,
			llvm::IRBuilder<> &builder,
			scope_t::ref scope,
			life_form_t life_form) const;

	void exempt_life_release() const;

	/* create a new life */
	life_t::ref new_life(status_t &status, life_form_t life_form);

	const status_t &status_tracker;
	const life_t::ref former_life;
	const life_form_t life_form;
	std::vector<bound_var_t::ref> values;

private:
	mutable bool release_vars_called;
};

void call_addref_var(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_var_t::ref var,
		std::string reason);
void call_release_var(
		status_t &status,
		llvm::IRBuilder<> &builder,
		scope_t::ref scope,
		bound_var_t::ref var,
		std::string reason);
void life_dump(ptr<const life_t> life);
