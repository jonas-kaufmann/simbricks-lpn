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
#include <string.h>

#include <cstdint>
#include <iostream>
#include <numeric>

#include "include/gcd_verilator.h"
#include "include/vfio.h"

volatile uint16_t result;

uint16_t hw_gcd(volatile GcdState &gcd_unit, uint16_t first_int,
                uint16_t second_int) {
  GcdInputs inputs{};
  inputs.req_msg = first_int << 16 | second_int;
  inputs.req_val = 1;
  *(volatile uint64_t *)&gcd_unit.inputs = *(uint64_t *)&inputs;

  GcdOutputs outputs;
  do {
    *(uint32_t *)&outputs = *(volatile uint32_t *)&gcd_unit.outputs;
  } while (outputs.resp_val == 0);

  uint16_t result = outputs.resp_msg;

  inputs.req_val = 0;
  inputs.resp_rdy = 1;
  *(volatile uint64_t *)&gcd_unit.inputs = *(uint64_t *)&inputs;

  return result;
}

int main(int argc, char **argv) {
  if (argc != 5) {
    std::cerr << "Usage: gcd_hw_workload PCI-DEVICE A B ITERATIONS\n";
    return EXIT_FAILURE;
  }

  int vfio_fd = vfio_init(argv[1]);
  if ((vfio_fd) < 0) {
    std::cerr << "vfio init failed" << std::endl;
    return 1;
  }

  size_t reg_len;
  void *mapped_addr;
  if (vfio_map_region(vfio_fd, 0, &mapped_addr,
                      &reg_len)) {
    std::cerr << "vfio map region failed" << std::endl;
    return 1;
  }

  if (vfio_busmaster_enable(vfio_fd)) {
    std::cerr << "vfio busmaster enable failed" << std::endl;
    return 1;
  }

  // ordering of accesses to this struct is crucial, as every read and write
  // will cause PCI BAR reads and writes
  volatile GcdState &gcd_unit = *static_cast<volatile GcdState *>(mapped_addr);

  uint16_t first_int = std::stoi(argv[2]);
  uint16_t second_int = std::stoi(argv[3]);
  int iterations = std::stoi(argv[4]);

  uint16_t correct_result = std::gcd(first_int, second_int);

  // gcd_unit.ctrl.tracing_active = true;
  // gcd_unit.inputs.req_msg = 1;
  // gcd_unit.inputs.req_msg = 0;
  // gcd_unit.inputs.req_msg = 1;
  // gcd_unit.inputs.req_msg = 0;
  // gcd_unit.inputs.req_msg = 1;
  // gcd_unit.ctrl.tracing_active = false;

  std::cout << "read req rdy" << std::endl;
  while (gcd_unit.outputs.req_rdy == 0) {
  }
  std::cout << "device rdy" << std::endl;

  // gcd_unit.ctrl.tracing_active = true;
  std::cout << "set tracing to active" << std::endl;
  for (int i = 0; i < iterations; ++i) {
    result = hw_gcd(gcd_unit, first_int, second_int);
    if (result != correct_result) {
      std::cerr << "gcd_hw_workload: got " << result << " expected "
                << correct_result << "\n";
    }
  }

  // gcd_unit.ctrl.tracing_active = false;
  return 0;
}
