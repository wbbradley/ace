#pragma once
#include <memory>

#include "ast.h"
#include "unification.h"
#include "user_error.h"

namespace gen {
struct block_t;

struct value_t {
    typedef std::shared_ptr<value_t> ref;
    typedef std::vector<ref> refs;

    value_t(location_t location,
            std::weak_ptr<block_t> parent,
            types::type_t::ref type,
            std::string name)
        : location(location), parent(parent), type(types::unitize(type)),
          name(name.size() == 0 ? bitter::fresh() : name) {
    }
    virtual ~value_t() {
    }
    virtual std::string str() const = 0;
    virtual std::ostream &render(std::ostream &os) const = 0;
    location_t get_location() const {
        return location;
    }
    virtual void set_name(identifier_t id);
    virtual std::string get_name() const;
    location_t const location;
    types::type_t::ref const type;

    std::weak_ptr<block_t> parent;
    std::string name;
};

struct proxy_value_t : public value_t {
    proxy_value_t(location_t location,
                  std::weak_ptr<block_t> parent,
                  std::string name,
                  types::type_t::ref type)
        : value_t(location, parent, type, name) {
    }

    void set_proxy_impl(value_t::ref impl_);

    std::string str() const override;
    std::ostream &render(std::ostream &os) const override;

    void set_name(identifier_t id) override;

    value_t::ref impl;
};

typedef std::unordered_map<std::string,
                           std::map<types::type_t::ref, value_t::ref, types::compare_type_t>>
    env_t;
value_t::ref maybe_get_env_var(const env_t &env, identifier_t id, types::type_t::ref type);
value_t::ref get_env_var(const env_t &env, identifier_t id, types::type_t::ref type);
void set_env_var(env_t &env,
                 std::string name,
                 value_t::ref value,
                 bool allow_shadowing = false);

struct module_t {
    typedef std::shared_ptr<module_t> ref;

    env_t env;
};

struct function_t;

struct instruction_t : public value_t {
    typedef std::shared_ptr<instruction_t> ref;

    std::string str() const override final;

    instruction_t(location_t location,
                  types::type_t::ref type,
                  std::weak_ptr<block_t> parent,
                  std::string name = "")
        : value_t(location, parent, type, name) {
    }

    virtual ~instruction_t() {
    }
};

typedef std::list<instruction_t::ref> instructions_t;

struct unit_t : public value_t {
    unit_t(location_t location, std::weak_ptr<block_t> parent)
        : value_t(location, parent, type_unit(INTERNAL_LOC()), "") {
    }
    std::string str() const override {
        return C_GOOD "()" C_RESET;
    }
    std::ostream &render(std::ostream &os) const override {
        return os << str();
    }
};

struct literal_t : public instruction_t {
    literal_t(token_t token, std::weak_ptr<block_t> parent, types::type_t::ref type)
        : instruction_t(token.location, type, parent), token(token) {
    }

    std::ostream &render(std::ostream &os) const override;

    token_t const token;
};

struct phi_node_t : public instruction_t {
    typedef std::shared_ptr<phi_node_t> ref;
    phi_node_t(location_t location, std::weak_ptr<block_t> parent, types::type_t::ref type)
        : instruction_t(location, type, parent) {
        if (type_equality(type, type_unit(INTERNAL_LOC()))) {
            throw user_error(location,
                             "it is unnecessary to use phi nodes on unit typed values");
        }
    }

    std::ostream &render(std::ostream &os) const override;

    void add_incoming_value(value_t::ref value, std::shared_ptr<block_t> block);

    std::vector<std::pair<value_t::ref, std::shared_ptr<block_t>>> incoming_values;
};

struct block_t {
    typedef std::shared_ptr<block_t> ref;

    block_t(std::weak_ptr<function_t> parent, std::string name) : parent(parent), name(name) {
    }
    std::string str() const;

    phi_node_t::ref get_phi_node();

    std::weak_ptr<function_t> parent;
    std::string const name;
    instructions_t instructions;

    struct comparator_t {
        bool operator()(const block_t::ref &a, const block_t::ref &b) const {
            return a->name < b->name;
        }
    };
};

struct cast_t : public instruction_t {
    cast_t(location_t location,
           std::weak_ptr<block_t> parent,
           value_t::ref value,
           types::type_t::ref type,
           std::string name)
        : instruction_t(location, type, parent, name), value(value) {
    }

    std::ostream &render(std::ostream &os) const override;

    value_t::ref value;
};

struct argument_t;

struct function_t : public value_t {
    typedef std::shared_ptr<function_t> ref;
    typedef std::weak_ptr<function_t> wref;

    function_t(module_t::ref module,
               std::string name,
               location_t location,
               types::type_t::ref type)
        : value_t(location, {}, type, name), parent(module) {
        assert(dyncast<const types::type_operator_t>(type));
    }

    std::string str() const override;
    std::ostream &render(std::ostream &os) const override;

    std::weak_ptr<module_t> parent;
    std::vector<block_t::ref> blocks;
    std::vector<std::shared_ptr<argument_t>> args;
};

struct builtin_t : public instruction_t {
    typedef std::shared_ptr<builtin_t> ref;

    builtin_t(location_t location,
              std::weak_ptr<block_t> parent,
              identifier_t id,
              value_t::refs params,
              types::type_t::ref type,
              std::string name)
        : instruction_t(location, type, parent, name), id(id), params(params) {
    }

    std::ostream &render(std::ostream &os) const override;

    identifier_t id;
    value_t::refs params;
};

struct argument_t : public value_t {
    typedef std::shared_ptr<argument_t> ref;
    argument_t(identifier_t id, types::type_t::ref type, int index, function_t::wref function)
        : value_t(id.location, {}, type, id.name), index(index), function(function) {
    }

    std::string str() const override;
    std::ostream &render(std::ostream &os) const override;

    int index;
    function_t::wref function;
};

struct goto_t : public instruction_t {
    goto_t(location_t location, std::weak_ptr<block_t> parent, block_t::ref branch)
        : instruction_t(location, type_unit(INTERNAL_LOC()), parent), branch(branch) {
    }

    std::ostream &render(std::ostream &os) const override;

    block_t::ref branch;
};

struct cond_branch_t : public instruction_t {
    cond_branch_t(location_t location,
                  std::weak_ptr<block_t> parent,
                  value_t::ref cond,
                  block_t::ref truthy_branch,
                  block_t::ref falsey_branch,
                  std::string name)
        : instruction_t(location, type_unit(INTERNAL_LOC()), parent, name), cond(cond),
          truthy_branch(truthy_branch), falsey_branch(falsey_branch) {
    }

    std::ostream &render(std::ostream &os) const override;

    value_t::ref cond;
    block_t::ref truthy_branch;
    block_t::ref falsey_branch;
};

struct callsite_t : public instruction_t {
    callsite_t(location_t location,
               std::weak_ptr<block_t> parent,
               value_t::ref callable,
               value_t::refs params,
               std::string name,
               types::type_t::ref return_type)
        : instruction_t(location, return_type, parent, name), callable(callable),
          params(params) {
    }

    std::ostream &render(std::ostream &os) const override;

    value_t::ref callable;
    value_t::refs params;
};

struct return_t : public instruction_t {
    return_t(location_t location, std::weak_ptr<block_t> parent, value_t::ref value)
        : instruction_t(location, type_unit(INTERNAL_LOC()), parent), value(value) {
    }

    std::ostream &render(std::ostream &os) const override;

    value_t::ref value;
};

struct load_t : public instruction_t {
    load_t(location_t location,
           std::weak_ptr<block_t> parent,
           value_t::ref rhs,
           std::string name)
        : instruction_t(location, type_deref(rhs->type), parent, name), rhs(rhs) {
    }

    std::ostream &render(std::ostream &os) const override;

    value_t::ref rhs;
};

struct store_t : public instruction_t {
    std::ostream &render(std::ostream &os) const override;

    store_t(location_t location,
            std::weak_ptr<block_t> parent,
            value_t::ref lhs,
            value_t::ref rhs,
            std::string name)
        : instruction_t(location, type_bottom(), parent, name), lhs(lhs), rhs(rhs) {
    }
    value_t::ref lhs, rhs;
};

types::type_t::ref tuple_type(const std::vector<value_t::ref> &dims);

struct tuple_t : public instruction_t {
    std::ostream &render(std::ostream &os) const override;
    tuple_t(location_t location,
            std::weak_ptr<block_t> parent,
            std::vector<value_t::ref> dims,
            std::string name)
        : instruction_t(location, tuple_type(dims), parent, name), dims(dims) {
    }

    std::vector<value_t::ref> dims;
};

struct tuple_deref_t : public instruction_t {
    tuple_deref_t(location_t location,
                  std::weak_ptr<block_t> parent,
                  value_t::ref value,
                  int index,
                  std::string name)
        : instruction_t(location,
                        tuple_deref_type(location, value->type, index),
                        parent,
                        name),
          value(value), index(index) {
    }
    std::ostream &render(std::ostream &os) const override;

    value_t::ref value;
    int index;
};

value_t::ref resolve_proxy(value_t::ref value);

struct builder_t {
    typedef gen::builder_t saved_state;
    builder_t(module_t::ref module) : module(module) {
    }
    builder_t(function_t::ref function) : module(function->parent), function(function) {
    }

    block_t::ref create_block(std::string name, bool insert_in_new_block = true);

    builder_t save_ip() const;
    void restore_ip(const builder_t &builder);

    void set_insertion_block(block_t::ref block);

    value_t::ref create_unit(location_t location, std::string name = "");
    value_t::ref create_builtin(identifier_t id,
                                const value_t::refs &values,
                                types::type_t::ref type,
                                std::string name = "");
    value_t::ref create_literal(token_t token, types::type_t::ref type);
    value_t::ref create_call(value_t::ref callable,
                             const value_t::refs &params,
                             std::string name = "");
    value_t::ref create_call(value_t::ref callable,
                             const value_t::refs &params,
                             types::type_t::ref type,
                             std::string name = "");
    value_t::ref create_cast(location_t location,
                             value_t::ref value,
                             types::type_t::ref type,
                             std::string name = "");
    value_t::ref create_tuple(location_t location,
                              const std::vector<value_t::ref> &dims,
                              std::string name = "");
    value_t::ref create_tuple_deref(location_t location,
                                    value_t::ref value,
                                    int index,
                                    std::string name = "");
    value_t::ref create_branch(location_t location, block_t::ref block);
    value_t::ref create_cond_branch(value_t::ref cond,
                                    block_t::ref truthy_branch,
                                    block_t::ref falsey_branch,
                                    std::string name = "");
    value_t::ref create_return(value_t::ref expr);
    function_t::ref create_function(std::string name,
                                    identifiers_t param_ids,
                                    location_t location,
                                    types::type_t::ref type);
    void insert_instruction(instruction_t::ref instruction);
    void merge_value_into(location_t location,
                          value_t::ref incoming_value,
                          block_t::ref merge_block);
    phi_node_t::ref get_current_phi_node();
    void ensure_terminator(std::function<void(builder_t &)> callback);

  public:
    module_t::ref module;
    function_t::ref function;
    block_t::ref block;
    block_t::ref break_to_block;
    block_t::ref continue_to_block;
    // std::shared_ptr<std::iterator<std::output_iterator_tag, void, void, void, void>>
    // inserter;
};

phi_node_t::ref phi_node(types::type_t::ref type);
value_t::ref gen(std::string name,
                 builder_t &builder,
                 const bitter::expr_t *expr,
                 const tracked_types_t &typing,
                 const env_t &env,
                 const std::unordered_set<std::string> &globals);
} // namespace gen
