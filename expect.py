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
        expect = (subprocess.check_output(
            'grep "^# expect: " < %s' % args.program, shell=True) or "").strip()
    except subprocess.CalledProcessError as e:
        expect = None

    if not expect:
        sys.exit(0)

    expect = expect[len("# expect: "):]

    print("Searching for %s in output from %s..." % (expect, args.program))
    try:
        actual = subprocess.check_output("./zion run %s" % args.program, shell=True)
    except subprocess.CalledProcessError as e:
        print(e)
        sys.exit(-1)

    print("-" * 10 + " " + args.program + " " + "-" * 20)
    print(actual)
    if actual.find(expect) == -1:
        sys.exit(-1)
    else:
        sys.exit(0)

if __name__ == '__main__':
    main()
