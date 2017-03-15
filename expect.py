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
        program_filename = subprocess.check_output(
            "./zionc find %s" % args.program, shell=True).strip()

        if not program_filename:
            print("Zion could not find %s" % args.program)
            sys.exit(0)

        expect = (subprocess.check_output(
            'grep "^# expect: " < %s' % program_filename, shell=True) or "").strip()

        if not expect:
            print("Could not find 'expect' declaration in %s" % program_filename)
            sys.exit(0)
    except subprocess.CalledProcessError as e:
        print(e)
        sys.exit(-1)

    expect = expect[len("# expect: "):]

    print("Searching for %s in output from %s..." % (expect, program_filename))
    try:
        actual = subprocess.check_output("./zionc run %s" % args.program, shell=True)
    except subprocess.CalledProcessError as e:
        print(e)
        sys.exit(-1)

    print("-" * 10 + " " + program_filename + " " + "-" * 20)
    print(actual)
    if actual.find(expect) == -1:
        sys.exit(-1)
    else:
        sys.exit(0)

if __name__ == '__main__':
    main()
