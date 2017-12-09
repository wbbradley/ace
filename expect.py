import sys
import argparse
import subprocess


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


def main():
    parser = _get_argparser()
    args = _parse_args(parser)

    try:
        expects = (
            subprocess.check_output('grep "^# expect: " < %s' % args.program, shell=True)
            or ""
        ).strip().split('\n')
    except subprocess.CalledProcessError as e:
        expects = []

    if not expects:
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

        msg = "Searching for %s in output from %s..." % (expect, args.program)

        if actual.find(expect) == -1:
            print(msg + " error.")
            print(actual)
            sys.exit(-1)
        else:
            print(msg + " success.")
            continue

    sys.exit(0)

if __name__ == '__main__':
    main()
