#pragma once
#include "ptr.h"
#include "bound_var.h"
#include "status.h"
#include "llvm_utils.h"

#define lf_function  0x01
#define lf_block     0x02
#define lf_loop      0x04
#define lf_statement 0x08

/* life_form_t is another word for scope or extent. In Zion, scope and extent
 * should be equivalent although there are unnamed values that need treatment at
 * the statement level. these may not have names and thusly are not mentioned in
 * scopes. */
class life_form_t {
public:
	life_form_t(int val);

	bool is_function() const { return val & lf_function; }
	bool is_loop() const { return val & lf_loop; }
	bool is_statement() const { return val & lf_statement; }
	bool is_block() const { return val & lf_block; }
	bool operator ==(const life_form_t rhs) const {
	   	return (val & rhs.val);
   	}
	bool operator !=(const life_form_t rhs) const { return !(*this == rhs); }

private:
	int val;
};

struct life_t : std::enable_shared_from_this<life_t> {
	typedef ptr<life_t> ref;

	life_t(life_form_t life_form, life_t::ref former_life=nullptr);
	~life_t();

	life_t(const life_t &life) = delete;
	life_t &operator =(const life_t &life) = delete;

	std::string str() const;

	/* track a value for later release at a given life_form level */
	void track_var(
			llvm::IRBuilder<> &builder,
			scope_t::ref scope,
			bound_var_t::ref value,
			life_form_t track_in_life_form);

	/* release values down to and including a particular life_form level */
	void release_vars(
			llvm::IRBuilder<> &builder,
			scope_t::ref scope,
			life_form_t life_form) const;

	void exempt_life_release() const;

	/* create a new life */
	life_t::ref new_life(life_form_t life_form);

	const life_t::ref former_life;
	const life_form_t life_form;
	std::vector<bound_var_t::ref> values;

private:
	mutable bool release_vars_called;
};

void life_dump(ptr<const life_t> life);
