import sys
import argparse
import subprocess
try:
    from termcolor import colored
except:
    colored = lambda t, c: t


def color(text, col):
    if not sys.stdout.isatty():
        return text
    else:
        return colored(text, col)


def _get_argparser():
    """Pull the arguments from the CLI."""
    parser = argparse.ArgumentParser(description='Test a Zion program.')

    parser.add_argument(
        '-p', '--program',
        required=True,
        type=str, help='the program you would like to run')

    return parser


def _parse_args(parser):
    return parser.parse_args(sys.argv[1:])


def gather_expects(progname):
    try:
        return (
            subprocess.check_output('grep "^# expect: " < %s' % progname, shell=True)
            or ""
        ).strip().split('\n')
    except subprocess.CalledProcessError as e:
        return []


def gather_rejects(progname):
    try:
        return (
            subprocess.check_output('grep "^# reject: " < %s' % progname, shell=True)
            or ""
        ).strip().split('\n')
    except subprocess.CalledProcessError as e:
        return []


def main():
    parser = _get_argparser()
    args = _parse_args(parser)

    expects = gather_expects(args.program)
    rejects = gather_rejects(args.program)

    if not expects and not rejects:
        sys.exit(0)

    try:
        print("-" * 10 + " " + args.program + " " + "-" * 20)
        cmd = "./zion run %s" % args.program
        print("running " + cmd)
        actual = subprocess.check_output(cmd, shell=True)
    except subprocess.CalledProcessError as e:
        print(e)
        print(e.output)
        sys.exit(-1)

    for expect in expects:
        expect = expect[len("# expect: "):]

        msg = "Searching for %s in output from %s..." % (color(expect, "green"), args.program)

        if actual.find(expect) == -1:
            print(msg + color(" error", "red") + ".")
            print(actual)
            sys.exit(-1)
        else:
            print(msg + color(" success", "green") + ".")
            continue

    for reject in rejects:
        reject = reject[len("# reject: "):]

        msg = "Hoping to not see %s in output from %s..." % (color(reject, "red"), args.program)

        if actual.find(reject) != -1:
            print(msg + color(" error", "red") + ".")
            print(actual)
            sys.exit(-1)
        else:
            print(msg + color(" success", "green") + ".")
            continue

    sys.exit(0)

if __name__ == '__main__':
    main()
