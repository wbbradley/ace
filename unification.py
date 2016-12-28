#!/usr/bin/env python
# coding=utf8

"""
Unification for Algebraic Data Types with Macros.
Copyright (c) 2016 William Bradley
The MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Attribution: Aspects of this implementation were inspired by Robert
Smallshire's implementation of Andrew Forrest's Scala implementation of
Hindley/Milner Algorithm W.
"""

import json
import os
import copy

UNREACHABLE = 'void'


def error(x):
    return "\033[31m%s\033[0m" % x


def success(x):
    return "\033[32m%s\033[0m" % x


def yellow(x):
    return "\033[33m%s\033[0m" % x


def log(msg, level=1):
    dbg_level = os.environ.get("DEBUG", 0)
    if int(dbg_level) >= int(level):
        print(msg)


class TypeId:

    def __init__(self, name):
        assert isinstance(name, str)
        self.name = name

    def __str__(self):
        assert not "Must have bindings context."

    def to_str(self, bindings):
        return self.name

    def __repr__(self):
        return "TypeId('%s')" % self.name

    def rebind(self, bindings):
        instance = bindings.get(self.name, None)
        if instance:
            return instance.rebind(bindings)
        return self


global __var_counter
__var_counter = 0


def fresh_var():
    global __var_counter
    try:
        return "__%d" % __var_counter
    finally:
        __var_counter += 1


class TypeVariable:

    def __init__(self, name=None):
        if name is None:
            name = fresh_var()
        assert isinstance(name, str)
        self.name = name

    def __str__(self):
        assert not "Must have bindings context."

    def to_str(self, bindings):
        instance = bindings.get(self.name, None)
        if instance:
            return instance.to_str(bindings)
        else:
            return "(any %s)" % self.name

    def __repr__(self):
        return "TypeVariable(%r)" % self.name

    def rebind(self, bindings):
        instance = bindings.get(self.name, None)
        if instance:
            return instance.rebind(bindings)
        return self


class TypeOperator:

    def __init__(self, operator, operand):
        assert not isinstance(operator, str)
        self.operator = operator
        self.operand = operand

    def __str__(self):
        assert not "Must have bindings context."

    def __repr__(self):
        return "TypeOperator(%r, %r)" % (self.operator, self.operand)

    def to_str(self, bindings):
        assert self.operator

        if self.operand:
            return "(%s %s)" % (self.operator.to_str(bindings),
                                self.operand.to_str(bindings))
        else:
            return self.operator.to_str(bindings)

    def rebind(self, bindings):
        return TypeOperator(self.operator.rebind(bindings),
                            self.operand.rebind(bindings))


class TypeProduct:

    def __init__(self, dimensions):
        self.dimensions = dimensions

    def __str__(self):
        assert not "Must have bindings context."

    def __repr__(self):
        return "TypeProduct(%r)" % self.dimensions

    def to_str(self, bindings):
        assert self.dimensions is not None

        return "(and %s)" % ' '.join(
            dimension.to_str(bindings) for dimension in self.dimensions)

    def rebind(self, bindings):
        return TypeProduct([dimension.rebind(bindings)
                            for dimension in self.dimensions])


class TypeSum:

    def __init__(self, options):
        self.options = options

    def __str__(self):
        assert not "Must have bindings context."

    def __repr__(self):
        return "TypeSum(%r)" % self.options

    def to_str(self, bindings):
        assert self.options is not None

        return "(or %s)" % ' '.join(
            option.to_str(bindings) for option in self.options)

    def rebind(self, bindings):
        return TypeSum([option.rebind(bindings)
                        for option in self.options])


def prune(t, bindings):
    """Follow the links across the bindings to reach the final binding."""
    if isinstance(t, TypeVariable):
        if t.name in bindings:
            return prune(bindings[t.name], bindings)
        else:
            return t
    return t


def occurs_in_type(v, type2, bindings):
    """See whether a type variable occurs in a type expression."""
    pruned_type2 = prune(type2, bindings)

    if pruned_type2 == v:
        return True
    elif isinstance(pruned_type2, TypeOperator):
        return occurs_in_type(v, pruned_type2.operand, bindings)

    # TODO: also check inside of TypeSums and TypeProducts

    return False


def unify_terms(env, outbound, inbound):
    """Unifies type terms.
    Returns whether we succeeded, the type, and the bindings used to
    achieve that type.
    """
    # Start by converting the type terms into operators and variables, to
    # simplify things for unification.
    log("unifying terms %s <- %s" % (outbound, inbound), 3)

    outbound_type = outbound.evaluate(env).get_type()
    inbound_type = inbound.evaluate(env).get_type()

    ret, details, bindings = unify(outbound_type, inbound_type, env, {})

    if ret:
        details = outbound_type.to_str(bindings)

    return ret, details, bindings


def no_cycles(unify):
    """Prevent cycles during unification."""
    _visited = {}

    def wrapper(outbound_type, inbound_type, env, bindings):
        log("= %s\n- %s\n" % (outbound_type.to_str(bindings),
                              inbound_type.to_str(bindings)), 2)

        pruned_a = prune(outbound_type, bindings)
        pruned_b = prune(inbound_type, bindings)

        a = pruned_a
        b = pruned_b

        # memoize and do cycle detection with bound terms stringified
        params = "%s|%s|%r" % (
            a.to_str(bindings),
            b.to_str(bindings),
            bindings)

        if params in _visited:
            # memoize and detect recurring calls
            if type(_visited[params]) == bool:
                print("saw already %s" % _visited[params])
                assert False

            return _visited[params]
        else:
            _visited[params] = False
        _visited[params] = unify(outbound_type, inbound_type, env, bindings)
        return _visited[params]
    return wrapper


@no_cycles
def unify(outbound_type, inbound_type, env, bindings):
    # print("Unifying %r with %r" % (outbound_type, inbound_type))
    a = prune(outbound_type, bindings)
    b = prune(inbound_type, bindings)

    if a.to_str(bindings) == b.to_str(bindings):
        return True, '', bindings

    if isinstance(a, TypeVariable):
        if a != b:
            if occurs_in_type(a, b, bindings):
                return False, "recursive unification", bindings

            assert a.name not in bindings

            bindings = bindings.copy()
            bindings[a.name] = b

        return True, '', bindings

    elif isinstance(b, TypeVariable):
        return unify(b, a, env, bindings)
    elif isinstance(a, TypeProduct):
        if not isinstance(b, TypeProduct):
            return (False, "inbound type is not a product type", {})

        if len(a.dimensions) != len(b.dimensions):
            return (False, "product type lengths do not match", {})

        for a_dim, b_dim in zip(a.dimensions, b.dimensions):
            ret, reason, new_bindings = unify(a_dim, b_dim, env, bindings)
            if not ret:
                return ret, reason, {}
            bindings = new_bindings

        return (True, 'products match', bindings)

    elif isinstance(a, TypeSum):
        # the outbound type has options, let's make sure that all possible
        # types in the inbound type can be matched to the outbound type.
        if not isinstance(b, TypeSum):
            reasons = []
            for option in a.options:
                ret, reason, new_bindings = unify(option, b, env, bindings)
                if ret:
                    assert new_bindings is not None
                    bindings = new_bindings

                    # the inbound type matches this option
                    return (
                        ret, 'matches %s' % option.to_str(bindings), bindings)
                reasons.append(reason)

            return (
                False, "inbound type not in the polymorph - \n\t%s" % (
                    '\n\t'.join(reasons)), {})

        else:
            assert isinstance(b, TypeSum)

            # the inbound type also has options! all inbound type possibilities
            # must be unifiable against the outbound options.
            for inbound_option in b.options:
                log("checking inbound %r against %r" % (
                    inbound_option, a), 8)
                ret, reason, new_bindings = unify(a, inbound_option, env,
                                                  bindings)
                if ret:
                    # unification succeeded, accept the bindings
                    bindings = new_bindings
                else:
                    return (
                        ret,
                        "\n\tcould not find a match for \n\t\t%s"
                        "\n\tin\n\t\t%s" % (
                            inbound_option.to_str(bindings),
                            a.to_str(bindings)),
                        bindings)

            assert bindings is not None

            return True, 'inbound type is a subset of outbound type', bindings

    elif isinstance(a, TypeOperator):
        if isinstance(b, TypeOperator):
            # Two type operators, first try to unify the operands
            ret, reason, new_bindings = unify(a.operator, b.operator, env,
                                              bindings)
            if ret:
                assert new_bindings is not None

                if bool(a.operand is None) != bool(b.operand is None):
                    return (
                        False,
                        "Type mismatch: %s != %s" % (a.to_str(new_bindings),
                                                     b.to_str(new_bindings)),
                        {})

                if a.operand and b.operand:
                    return unify(a.operand, b.operand, env, new_bindings)
                else:
                    return True, '', new_bindings

        # attempt expanding the left side
        new_a = eval_apply(a.operator, a.operand, env, bindings)
        if new_a:
            return unify(new_a, b, env, bindings)
        else:
            return (
                False, "%s <> %s /* eval */" % (a.operator.to_str(bindings),
                                     b.operator.to_str(bindings)), {})

    else:
        return (
            False, "%s <> %s" % (a.to_str(bindings),
                                 b.to_str(bindings)), {})


class Lambda:
    __slots__ = ['var', 'body']

    def __init__(self, var, body):
        self.var = var
        self.body = body

    def __repr__(self):
        return "Lambda(%r, %r)" % (self.var, self.body)


def eval_apply(operator, operand, env, bindings):
    assert isinstance(operator, TypeId)

    # See if there is a function in the environment for this operator
    fn = env.get(operator.name)

    if fn:
        # Bind the function's parameter name to the operand
        bindings = copy.copy(bindings)

        assert isinstance(fn.var, TypeId)
        print("Binding %r to %r" % (fn.var.name, operand))
        bindings[fn.var.name] = operand

        return fn.body.rebind(bindings)

    else:
        return None


def main():
    env = {
        'Node': Lambda(TypeId('T'),
                       TypeProduct([
                           TypeId('T'),
                           TypeOperator(TypeId('List'), TypeId('T'))])),
        'List': Lambda(TypeId('T'),
                       TypeSum([
                           TypeId('Null'),
                           TypeOperator(TypeId('Node'), TypeId('T'))])),
        'Maybe': Lambda(TypeId('T'),
                       TypeSum([
                           TypeId('Empty'),
                           TypeOperator(TypeId('Just'), TypeId('T'))])),
    }

    NodeInt = TypeOperator(TypeId('Node'), TypeId('int'))
    MaybeInt = TypeOperator(TypeId('Maybe'), TypeId('int'))
    AnyInt = TypeOperator(TypeVariable(), TypeId('int'))
    ProdIn = TypeProduct([TypeId('int'), NodeInt])

    # ListInt = TypeOperator(TypeId('List'), TypeVariable())
    tv = TypeVariable()
    ListListInt = TypeProduct([
        tv,
        TypeOperator(TypeVariable(), TypeId('int'))])

    result, reason, bindings = unify(MaybeInt, TypeId('Empty'), env, {})
    print(result)
    print(reason)
    print(bindings)
    print("Final type = %s" % MaybeInt.rebind(bindings).to_str(bindings))


def bindings_to_str(bindings):
    return json.dumps(bindings, sort_keys=True, indent=4)


if __name__ == '__main__':
    main()
