# Copyright 2023 Max Planck Institute for Software Systems, and
# National University of Singapore
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
import argparse

arg_parser = argparse.ArgumentParser(
    'gem5_calc_energy',
    description=
    'Calculates energy and power estimates from gem5\'s stats.txt file for the '
    'whole time span logged in it. For every logged interval, power and energy '
    'are computed from the CPU statistics by adding static and dynamic power '
    'and scaling the dynamic power according to the load.'
)
arg_parser.add_argument(
    '--static', required=True, type=float, help='static power in Watts'
)
arg_parser.add_argument(
    '--dynamic',
    required=True,
    type=float,
    help='maximum dynamic power in Watts'
)
arg_parser.add_argument('filename')

args = arg_parser.parse_args()

power_estimates = []
energy_estimate = 0
total_seconds = 0

# for parsing
previous_committed_insts = 0
previous_cycles = 0
previous_seconds = 0

# parser
with open(args.filename, encoding='UTF-8') as file:
    while True:
        line = file.readline()
        while line and not line.startswith('sim_seconds'):
            line = file.readline()
        if not line:
            break
        current_seconds = float(line.split()[1])

        line = file.readline()
        while line and not line.startswith('system.switch_cpus.committedInsts'):
            line = file.readline()
        if not line:
            break
        current_committed_insts = int(line.split()[1])

        line = file.readline()
        while line and not line.startswith('system.switch_cpus.numCycles'):
            line = file.readline()
        if not line:
            break
        current_cycles = int(line.split()[1])

        time_span = current_seconds - previous_seconds
        total_seconds += time_span
        cycles = current_cycles - previous_cycles
        committed_insts = current_committed_insts - previous_committed_insts
        ipc = committed_insts / cycles

        current_power = args.static + args.dynamic * ipc
        power_estimates.append(current_power)
        energy_estimate += current_power * time_span

        previous_committed_insts = current_committed_insts
        previous_cycles = current_cycles
        previous_seconds = current_seconds

print('total seconds', total_seconds)
print('total energy:', energy_estimate, 'J')
print('average power:', sum(power_estimates) / len(power_estimates), 'W')
