#!/usr/bin/env python3
# Copyright (c) 2020 freetrader
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Compares bench_bitcoin outputs and displays percentage changes.

Changes are computed between a 'before_file' and 'after_file' which are
the captured standard output of the `bench_bitcoin` program.

Benchmarks that do not appear in both files are listed in a notice preceding
the per-benchmark output unless `--quiet` option is used.

All other benchmarks are compared and output in a combined table row which
lists benchmark names, the before/after values and percentage change of
median.

Before/after averages are also always computed but output only if the
`--average` (`-a`) option is used.

Output is plain text (CSV) by default, but can be switched to Markdown
table format using `--markdown` (`-m`).

The `--color` (`-c`) option makes colored output using ANSI escape sequences.
Percentage change values are colored green for improvement, red for
degradations, and not colored unless their absolute value deviates more than
an configurable percentage between before and after medians OR averages.
Coloring is disabled for Markdown.

The "highlighting" percentage cutoff is configurable using
`--percentage` (`-p`).
"""

import argparse
import csv
import os
import sys
import logging

# changes within this absolute percentage of BOTH median and mean are not highlighted
ABS_PERCENTAGE_NO_HIGHLIGHT = 1.0  # must be float

# Formatting. Default colors to empty strings.
BOLD, RED, GREEN, BLUE, YELLOW, GREY = ("", ""), ("", ""), ("", ""), ("", ""), ("", ""), ("", "")

if os.name == 'posix':
    # primitive formatting on supported
    # terminal via ANSI escape sequences:
    BOLD = ('\033[0m', '\033[1m')
    RED = ('\033[0m', '\033[0;31m')
    GREEN = ('\033[0m', '\033[0;32m')
    BLUE = ('\033[0m', '\033[0;34m')
    YELLOW = ('\033[0m', '\033[0;33m')
    GREY = ('\033[0m', '\033[1;30m')

# Set up logging
logging_level = logging.DEBUG
logging.basicConfig(format='%(message)s', level=logging_level)


class Benchmark(object):
    """Represents a single benchmark (obtained from a row in the output of a file)."""

    def __init__(self, filename, benchmark, evals, iterations, total, t_min, t_max, t_median):
        self.filename = filename
        self.benchmark = benchmark
        self.evals_str = evals
        self.evals = int(evals)
        self.iterations_str = iterations
        self.iterations = int(iterations)
        self.t_total_str = total
        self.t_total = float(total)
        self.t_min_str = t_min
        self.t_min = float(t_min)
        self.t_max_str = t_max
        self.t_max = float(t_max)
        self.t_median_str = t_median
        self.t_median = float(t_median)
        # Compute average
        self.t_avg = self.t_total / (self.evals * self.iterations)

    def __repr__(self):
        return ','.join([
            'benchmark:' + self.benchmark,
            'evals:' + self.evals_str,
            'iters:' + self.iterations_str,
            'total:' + self.t_total_str,
            'min:' + self.t_min_str,
            'max:' + self.t_max_str,
            'median:' + self.t_median_str,
            'avg:' + str(self.t_avg)])


class BenchmarkFile(object):
    """Represents a file of benchmarks."""

    def __init__(self, filename=None, benchmarks=None):
        # keep a map from name to a benchmark's data
        self.benchmarks_by_name = {}
        if filename:
            self.filename = filename
            self.benchmarks = self.from_file(filename)
        else:
            assert(isinstance(benchmarks, list))
            if isinstance(benchmarks, list):
                self.benchmarks = benchmarks
            else:
                self.benchmarks = []
            self.filename = None
        for b in self.benchmarks:
            self.benchmarks_by_name[b.benchmark] = b

    def from_file(self, filename):
        """Reads benchmarks from a file and returns a list of Benchmark objects"""

        benchmarks_data = []
        with open(filename, newline='', encoding='utf-8') as csvfile:
            csv_reader = csv.reader(csvfile)
            # Skip field comment header in bench_bitcoin's CSV output
            next(csv_reader)

            for row in csv_reader:
                benchmarks_data.append(Benchmark(filename, *row))
        return benchmarks_data

    def __getitem__(self, bench_name):
        """Return the benchmark object (full data) for a benchmark name"""
        try:
            return self.benchmarks_by_name[bench_name]
        except IndexError:
            return None

    def __repr__(self):
        s = self.filename + '\n'
        for b in self.benchmarks:
            s += repr(b) + '\n'
        return s


class BenchmarkFileComparator(object):
    """Compares two benchmark files."""

    def __init__(self, bench_file_before, bench_file_after):
        if isinstance(bench_file_before, str) and isinstance(bench_file_after, str):
            self.before_file_obj = BenchmarkFile(bench_file_before)
            self.after_file_obj = BenchmarkFile(bench_file_after)
        elif isinstance(bench_file_before, BenchmarkFile) and isinstance(bench_file_after, BenchmarkFile):
            self.before_file_obj = bench_file_before
            self.after_file_obj = bench_file_after
        else:
            raise TypeError

        self.before_benchmark_names = frozenset(b.benchmark for b in self.before_file_obj.benchmarks)
        self.after_benchmark_names = frozenset(b.benchmark for b in self.after_file_obj.benchmarks)

    def _benches_only_in_before_file(self):
        """Returns a list of benchmarks only in the 'before' file."""
        return list(self.before_benchmark_names - self.after_benchmark_names)

    def _benches_only_in_after_file(self):
        """Returns a list of benchmarks only in the 'after' file."""
        return list(self.after_benchmark_names - self.before_benchmark_names)

    def _common_benches(self):
        """Returns a list of benchmarks in both files."""
        return list(self.after_benchmark_names & self.before_benchmark_names)

    def output(self, show_averages=False, percentage=ABS_PERCENTAGE_NO_HIGHLIGHT, be_quiet=False, markdown=False, colorize=False):
        """Generate the comparison output."""
        benches_in_both = self._common_benches()
        if not benches_in_both:
            logging.warning("Warning: The files have no benchmarks in common - nothing to compare!")
            return

        color_on = 1 if colorize else 0
        if not be_quiet:
            only_in_before = self._benches_only_in_before_file()
            if only_in_before:
                logging.warning(
                    "Warning: Unable to compare some benchmarks existing only in 'before' file:\n{}{}{}\n".format(
                        YELLOW[color_on], '\n'.join(
                            sorted(only_in_before)), YELLOW[0]))

            only_in_after = self._benches_only_in_after_file()
            if only_in_after:
                logging.warning(
                    "Warning: Unable to compare some benchmarks existing only in 'after' file:\n{}{}{}\n".format(
                        YELLOW[color_on], '\n'.join(
                            sorted(only_in_after)), YELLOW[0]))

        # Output the header
        if markdown:
            # markdown table header
            header_row = "| Benchmark | median_before | median_after | median_pct_change"
            if show_averages:
                header_row += " | avg_before | avg_after | avg_pct_change"
            header_row += " |"
            print(header_row)
            # separator line
            num_vert_bars = header_row.count('|')
            print('|' + '|'.join(["---", ] * (num_vert_bars - 1)) + '|')
        else:
            header_row = "# Benchmark,median_before,median_after,median_pct_change"
            if show_averages:
                header_row += ",avg_before,avg_after,avg_pct_change"
            print(header_row)

        # Output the rows
        for cb in sorted(benches_in_both):
            # Construct a list of elements in the row
            # Starting with the benchmark name...
            output_row_elems = [cb, ]

            median_before = self.before_file_obj[cb].t_median
            median_after = self.after_file_obj[cb].t_median
            median_pct_change = 100 * (median_after - median_before) / median_before
            output_row_elems += [median_before, median_after, median_pct_change]

            avg_before = self.before_file_obj[cb].t_avg
            avg_after = self.after_file_obj[cb].t_avg
            avg_pct_change = 100 * (avg_after - avg_before) / avg_before

            if show_averages:
                output_row_elems += [avg_before, avg_after, avg_pct_change]

            if markdown:
                # markdown row output
                row_str = "|" + " | ".join([str(x) for x in output_row_elems[0:3]])
                # median_pct_change
                row_str += ' | ' + "{:.2f}".format(output_row_elems[3])
                if show_averages:
                    # avg_before, avg_after
                    row_str += ' | ' + " | ".join([str(x) for x in output_row_elems[4:6]])
                    # avg_pct_change
                    row_str += ' | ' + "{:.2f}".format(output_row_elems[6])

                print(row_str.strip() + ' |')
            else:
                # plain text row output
                # name, median_before, median_after
                row_str = ",".join([str(x) for x in output_row_elems[0:3]])
                color_on_str = ''
                color_off_str = ''
                if colorize:
                    if abs(median_pct_change) > float(percentage) or abs(avg_pct_change) > float(percentage):
                        color_on_str, color_off_str = (
                            RED[color_on], RED[0]) if median_pct_change > 0.0 else (
                            GREEN[color_on], GREEN[0])
                # median_pct_change
                row_str += ',' + color_on_str + "{:.2f}".format(output_row_elems[3]) + color_off_str
                if show_averages:
                    # avg_before, avg_after
                    row_str += ',' + ",".join([str(x) for x in output_row_elems[4:6]])
                    # avg_pct_change
                    row_str += ',' + color_on_str + "{:.2f}".format(output_row_elems[6]) + color_off_str

                print(row_str)


def main():
    # Parse arguments and pass through unrecognised args
    parser = argparse.ArgumentParser(add_help=False,
                                     usage='%(prog)s [benchmark_diff.py options] before_file after_file',
                                     description=__doc__,
                                     epilog='''
    `before_file` and `after_file` must be outputs of bench_bitcoin program and include the commented header line.''',
                                     formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('--average', '-a', action='store_true',
                        help='Display computed average values (total / iterations)')
    parser.add_argument('--quiet', '-q', action='store_true',
                        help='Omit warnings about benchmarks not present in both files.')
    parser.add_argument('--percentage', '-p', type=float, default=ABS_PERCENTAGE_NO_HIGHLIGHT,
                        help='Absolute percentage change threshold above which to highlight, if coloring is on. Must be >= 0.0, decimal points allowed.')
    parser.add_argument('--markdown', '-m', action='store_true',
                        help='Instead of plain text output, emit a table in Markdown format.')
    parser.add_argument('--color', '-c', action='store_true',
                        help='Color plain text output using ANSI escape sequences')
    parser.add_argument('--help', '-h', '-?',
                        action='store_true', help='Show this help text and exit.')
    args, file_args = parser.parse_known_args()

    if args.help or not file_args:
        parser.print_help()
        sys.exit(0)

    if args.percentage < 0.0:
        print("ERROR: Percentage argument must be non-negative!")
        sys.exit(1)

    if len(file_args) != 2:
        print("ERROR: Need exactly two arguments - a before and after file!")
        sys.exit(1)

    before_file, after_file = file_args

    comparator = BenchmarkFileComparator(before_file, after_file)
    comparator.output(args.average, args.percentage, args.quiet, args.markdown, args.color)


if __name__ == '__main__':
    main()
