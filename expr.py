#!/usr/bin/python
# coding=utf8

"""
Lambda Calculus with Unification for Algebraic Data Types with Macros.
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

import sys
import os
import string

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
        print msg


class TermGeneric(object):
    """Generics can only be resolved by reductions or by unification."""

    def __init__(self, name):
        self.name = name

    def __str__(self):
        return "(any %s)" % self.name

    def __repr__(self):
        return "TermGeneric(%s)" % self.name

    def evaluate(self, env, macro_depth):
        # Only allow substitution of "any" type variables from the environment.
        return self

    def get_type(self):
        return TypeVariable(self.name)


class TermUnreachable(object):
    """An unreachable type."""

    def __str__(self):
        return UNREACHABLE

    def __repr__(self):
        return "TermUnreachable()"

    def evaluate(self, env, macro_depth):
        return self

    def get_type(self):
        return TypeUnreachable()


class TermId(object):
    """Identifier"""

    def __init__(self, name):
        self.name = name

    def __str__(self):
        return self.name

    def __repr__(self):
        return "TermId('%s')" % self.name

    def evaluate(self, env, macro_depth):
        value = env.get(self.name, self)
        if value is not self:
            return value.evaluate(env, macro_depth)
        else:
            return value

    def get_type(self):
        return TypeId(self.name)


class TermLambda(object):
    """TermLambda abstraction"""

    def __init__(self, v, body):
        assert isinstance(v, TermId)
        self.v = v
        self.body = body

    def __str__(self):
        return "(lambda {v} {body})".format(v=self.v, body=self.body)

    def __repr__(self):
        return "TermLambda(%s, %s)" % (repr(self.v), repr(self.body))

    def evaluate(self, env, macro_depth):
        log("Evaluating TermLambda %s" % self, 9)
        assert type(self.v) is TermId

        if self.v.name in env:
            env = env.copy()
            env.pop(self.v.name)

        return TermLambda(self.v, self.body.evaluate(env, macro_depth))

    def get_type(self):
        print "Lambdas must be beta-reduced before we can get their type."
        print repr(self)
        assert False


class TermSum(object):
    """Sum types."""

    def __init__(self, options):
        self.options = options

    def __str__(self):
        return "(or %s)" % ' '.join(str(option) for option in self.options)

    def __repr__(self):
        return "TermSum(%s)" % ', '.join(repr(option)
                                         for option in self.options)

    def evaluate(self, env, macro_depth):
        log("Evaluating TermSum %s" % self, 9)
        return TermSum([option.evaluate(env, macro_depth)
                        for option in self.options])

    def get_type(self):
        return TypeSum([option.get_type() for option in self.options])


class TermProduct(object):
    """Product types."""

    def __init__(self, dimensions):
        self.dimensions = dimensions

    def __str__(self):
        return "(and %s)" % ' '.join(
            str(dimension) for dimension in self.dimensions)

    def __repr__(self):
        return "TermProduct(%s)" % ', '.join(repr(dimension)
                                             for dimension in self.dimensions)

    def evaluate(self, env, macro_depth):
        log("Evaluating TermProduct %s" % self, 9)
        return TermProduct([dimension.evaluate(env, macro_depth)
                            for dimension in self.dimensions])

    def get_type(self):
        return TypeProduct([dimension.get_type()
                            for dimension in self.dimensions])


class TermApply(object):
    """Function application"""

    def __init__(self, fn, arg):
        self.fn = fn
        self.arg = arg

    def __str__(self):
        return "({fn} {arg})".format(fn=self.fn, arg=self.arg)

    def __repr__(self):
        return "TermApply(%s, %s)" % (repr(self.fn), repr(self.arg))

    def evaluate(self, env, macro_depth):
        log("Evaluating TermApply %s" % self, 9)
        fn = self.fn.evaluate(env, macro_depth)
        arg = self.arg.evaluate(env, macro_depth)

        if isinstance(fn, TermLambda):
            # We should only handle substitutions in lambdas when they are
            # being applied.
            env = env.copy()
            env[fn.v.name] = arg
            return fn.body.evaluate(env, macro_depth)
        else:
            return TermApply(fn, arg)

    def get_type(self):
        return TypeOperator(self.fn.get_type(),
                            self.arg.get_type())


class TermLet(object):
    """TermLet binding"""

    def __init__(self, v, defn, body):
        self.v = v
        self.defn = defn
        self.body = body

    def __str__(self):
        return "(let %s %s %s)" % (self.v, self.defn, self.body)

    def __repr__(self):
        return "TermLet(%s, %s, %s)" % (repr(self.v), repr(self.defn),
                                        repr(self.body))

    def evaluate(self, env, macro_depth):
        log("Evaluating TermLet %s" % self, 9)
        env = env.copy()
        env[self.v.name] = self.defn
        return self.body.evaluate(env, macro_depth)


class DefMacro(object):
    def __init__(self, name, body):
        self.name = name
        self.body = body

    def __str__(self):
        return u"(defmacro %s %s)" % (self.name, self.body)

    def __repr__(self):
        return u"DefMacro(%s, %s)" % (self.name, self.body)

    def evaluate(self, env, macro_depth):
        return self


class Unify(object):
    def __init__(self, outbound, inbound):
        self.outbound = outbound
        self.inbound = inbound

    def __str__(self):
        return u"(unify %s %s)" % (self.outbound, self.inbound)

    def __repr__(self):
        return u"Unify(%s, %s)" % (self.outbound, self.inbound)


def parse(text):
    expr = parse_expr(text)[1]
    assert len(expr) == 1
    return expr[0]


def _identifier_char(ch):
    return ch not in string.whitespace and ch not in ['(', ')']


def parse_expr(text, i=0):
    """Returns next i to read, and a list of sub-items"""
    items = []
    while i < len(text):
        while i < len(text) and text[i] in string.whitespace:
            i += 1

        if i >= len(text):
            assert False

        if text[i] == '(':
            i, item = parse_expr(text, i + 1)
            items.append(item)
        elif text[i] == ')':
            return i + 1, items
        else:
            start = i
            while (i < len(text) and
                   _identifier_char(text[i])):
                i += 1
            items.append(text[start:i])

    return i, items


def lambdify(p):
    if isinstance(p, (str, unicode)):
        return lambdify_build(parse(p))
    else:
        return lambdify_build(p)


def lambdify_build(p):
    if isinstance(p, (str, unicode)):
        if p == UNREACHABLE:
            return TermUnreachable()
        else:
            return TermId(p)

    if len(p) > 0:
        if p[0] == 'lambda':
            assert len(p) == 3
            return TermLambda(TermId(p[1]), lambdify(p[2]))
        elif p[0] == 'let':
            assert len(p) == 4
            return TermLet(TermId(p[1]),
                           lambdify(p[2]),
                           lambdify(p[3]))
        elif p[0] == 'any':
            assert len(p) == 2
            return TermGeneric(p[1])
        elif p[0] == 'or':
            return TermSum([lambdify(item) for item in p[1:]])
        elif p[0] == 'and':
            return TermProduct([lambdify(item) for item in p[1:]])
        elif p[0] == 'unify':
            assert len(p) == 3
            return Unify(lambdify(p[1]),
                         lambdify(p[2]))
        elif p[0] == 'defmacro':
            assert len(p) == 3
            return DefMacro(TermId(p[1]),
                            lambdify(p[2]))
        else:
            if len(p) == 1:
                return lambdify(p[0])
            else:
                return reduce(lambda x, y: (
                    TermApply(x, lambdify_build(y))),
                              p[1:], lambdify_build(p[0]))
    else:
        assert not "Term expressions must have elements"


class TypeId(object):

    def __init__(self, name):
        assert isinstance(name, (str, unicode))
        self.name = name

    def __str__(self):
        assert not "Must have bindings context."

    def to_str(self, bindings):
        return self.name

    def to_lambda(self, bindings):
        return TermId(self.name)

    def __repr__(self):
        return "TypeId('%s')" % self.name

    def fully_bind(self, bindings):
        return self


class TypeUnreachable(object):

    def __str__(self):
        assert not "Must have bindings context."

    def to_str(self, bindings):
        return UNREACHABLE

    def to_lambda(self, bindings):
        return TermUnreachable()

    def __repr__(self):
        return "TypeUnreachable()"

    def fully_bind(self, bindings):
        return self


class TypeVariable(object):

    def __init__(self, name):
        assert isinstance(name, (str, unicode))
        self.name = name

    def __str__(self):
        assert not "Must have bindings context."

    def to_str(self, bindings):
        instance = bindings.get(self.name, None)
        if instance:
            return instance.to_str(bindings)
        else:
            return "(any %s)" % self.name

    def to_lambda(self, bindings):
        instance = bindings.get(self.name, None)
        if instance:
            return instance.to_lambda(bindings)
        else:
            return TermGeneric(self.name)

    def __repr__(self):
        return "TypeVariable(%r)" % self.name

    def fully_bind(self, bindings):
        instance = bindings.get(self.name, None)
        if instance:
            return instance.fully_bind(bindings)
        else:
            # in theory, because this type is unknown, if we find it when
            # trying to bind to a type implementation before lowering, it must
            # be protected by a ref so that we don't have to know its size.
            return TypeUnreachable()


class TypeOperator(object):

    def __init__(self, operator, operand):
        assert not isinstance(operator, (str, unicode))
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

    def to_lambda(self, bindings):
        return TermApply(self.operator.to_lambda(bindings),
                         self.operand.to_lambda(bindings))

    def fully_bind(self, bindings):
        return TypeOperator(self.operator.fully_bind(bindings),
                            self.operand.fully_bind(bindings))


class TypeProduct(object):

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

    def to_lambda(self, bindings):
        return TermProduct([dimension.to_lambda(bindings)
                            for dimension in self.dimensions])

    def fully_bind(self, bindings):
        return TypeProduct([dimension.fully_bind(bindings)
                            for dimension in self.dimensions])


class TypeSum(object):

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

    def to_lambda(self, bindings):
        return TermSum([option.to_lambda(bindings)
                        for option in self.options])

    def fully_bind(self, bindings):
        return TypeSum([option.fully_bind(bindings)
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
                print "saw already %s" % _visited[params]
                assert False

            return _visited[params]
        else:
            _visited[params] = False
        _visited[params] = unify(outbound_type, inbound_type, env, bindings)
        return _visited[params]
    return wrapper


@no_cycles
def unify(outbound_type, inbound_type, env, bindings):
    pruned_a = prune(outbound_type, bindings)
    pruned_b = prune(inbound_type, bindings)

    a = pruned_a
    b = pruned_b

    if a.to_str(bindings) == b.to_str(bindings):
        return True, '', bindings

    if isinstance(a, TypeVariable):
        if a != b:
            if occurs_in_type(a, b, bindings):
                return False, "recursive unification", None

            assert a.name not in bindings

            bindings = bindings.copy()
            bindings[a.name] = b

        return True, '', bindings

    elif isinstance(a, TypeProduct):
        if not isinstance(b, TypeProduct):
            return (False, "inbound type is not a product type", None)

        if len(a.dimensions) != len(b.dimensions):
            return (False, "product type lengths do not match", None)

        for a_dim, b_dim in zip(a.dimensions, b.dimensions):
            ret, reason, new_bindings = unify(a_dim, b_dim, env, bindings)
            if not ret:
                return ret, reason, None
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
                    '\n\t'.join(reasons)), None)

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

    elif isinstance(a, TypeOperator) and isinstance(b, TypeOperator):
        ret, reason, bindings = unify(a.operator, b.operator, env, bindings)
        if not ret:
            return False, reason, None

        assert bindings is not None

        if bool(a.operand is None) != bool(b.operand is None):
            return (
                False,
                "Type mismatch: %s != %s" % (a.to_str(bindings),
                                             b.to_str(bindings)),
                None)

        if a.operand and b.operand:
            return unify(a.operand, b.operand, env, bindings)
    else:
        return (
            False, "%s <> %s" % (a.to_str(bindings),
                                 b.to_str(bindings)), None)


def use_readline(f):
    def wrapper(*args, **kwargs):
        try:
            import readline
            histfile = os.path.join(os.path.expanduser("~"), ".exprhist")
            readline.set_history_length(10000)
            try:
                readline.read_history_file(histfile)
            except IOError:
                pass
            import atexit
            atexit.register(readline.write_history_file, histfile)
        except:
            pass

        return f(*args, **kwargs)

    return wrapper


@use_readline
def main():
    env = {}
    input_count = 0
    if len(sys.argv) > 1:
        for filename in sys.argv[1:]:
            with open(filename) as f:
                lines = f.readlines()
                for line in lines:
                    line = line.strip()
                    if line and line[0] != '#':
                        evaluate('', line, env)

    while True:
        input_name = '\'%s' % input_count
        input_count += 1
        prompt = "\033[48;5;95;38;5;214m %s \033[0m " % input_name
        try:
            value = raw_input(prompt).strip()
        except EOFError:
            print "\nGoodbye."
            break

        if not value:
            input_count -= 1
            continue

        try:
            evaluate(input_name, value, env)

        except Exception as e:
            print error("Error %s" % e)
            raise


def bindings_to_str(bindings):
    assert bindings is not None

    ret = '{'

    for k, v in bindings.iteritems():
        ret += '\n\t%s: %s,' % (k, v.to_str(bindings))

    if bindings:
        ret += '\n'

    ret += '}'

    return ret


def evaluate(input_name, value, env):
    if not value:
        return

    if len(value) > 1 and value[0] == '.':
        if value[1:] == 'env':
            for k, v in env.iteritems():
                if len(k) < 2 or k[0] != "'":
                    print "%s: %s" % (k, v)
        else:
            print "Didn't understand %s" % value
    else:
        parsed = parse(value)
        term = lambdify(parsed)
        if input_name:
            env[input_name] = term

        if isinstance(term, Unify):
            print "Unifying %s <: %s" % (term.outbound, term.inbound)
            ret, details, bindings = unify_terms(env, term.outbound,
                                                 term.inbound)
            if ret:
                print success("Unified %s <: %s to %s with %s") % (
                    term.outbound, term.inbound,
                    details,
                    bindings_to_str(bindings))

                # recreate the final unified type name
                print "Final type is " + yellow("%s") % (
                    term.outbound
                    .get_type()
                    .fully_bind(bindings)
                    .to_lambda({}))
            else:
                print error("Unification %s <: %s failed: %s" % (
                    term.outbound, term.inbound, details))
        else:
            # DefMacro is the set! operator. It can mutate the env.
            if isinstance(term, DefMacro):
                env[str(term.name)] = term.body

            env['_'] = term.evaluate(env)
            print yellow(env['_'])


if __name__ == '__main__':
    main()
