#pragma once
namespace bitter {
	struct expr_t;
	struct var_t;
	struct predicate_t;
	struct pattern_block_t;
	using pattern_blocks_t = std::vector<pattern_block_t *>;
	struct match_t;
	struct type_decl_t;
	struct type_class_t;
	struct block_t;
	struct as_t;
	struct application_t;
	struct lambda_t;
	struct let_t;
	struct literal_t;
	struct conditional_t;
	struct return_statement_t;
	struct while_t;
	struct fix_t;
	struct decl_t;
	struct module_t;
	struct program_t;
}

std::ostream &operator <<(std::ostream &os, bitter::program_t *program);
std::ostream &operator <<(std::ostream &os, bitter::decl_t *decl);

