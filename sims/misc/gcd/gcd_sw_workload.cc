/*
 * Copyright 2023 Max Planck Institute for Software Systems, and
 * National University of Singapore
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <cstdint>
#include <iostream>
#include <string>

volatile uint16_t first_int;
volatile uint16_t second_int;
volatile uint16_t result;

uint16_t sw_gcd(uint16_t first_int, uint16_t second_int) {
  while (first_int != second_int) {
    if (first_int > second_int) {
      first_int = first_int - second_int;
    } else {
      second_int = second_int - first_int;
    }
  }
  return first_int;
}

int main(int argc, char **argv) {
  if (argc != 4) {
    std::cerr << "Usage: gcd_sw_workload A B ITERATIONS\n";
    return EXIT_FAILURE;
  }

  first_int = std::stoi(argv[1]);
  second_int = std::stoi(argv[2]);
  int iterations = std::stoi(argv[3]);

  for (int i = 0; i < iterations; ++i) {
    result = sw_gcd(first_int, second_int);
  }
}
