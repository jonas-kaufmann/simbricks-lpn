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
#ifndef SIMS_MISC_GCD_INCLUDE_GCD_VERILATOR_H_
#define SIMS_MISC_GCD_INCLUDE_GCD_VERILATOR_H_
#include <cstdint>

struct __attribute__((__packed__)) GcdInputs {
  uint8_t req_val;
  uint8_t reset;
  uint8_t resp_rdy;
  uint8_t dummy;
  uint32_t req_msg;
};

struct __attribute__((__packed__)) GcdOutputs {
  uint8_t req_rdy;
  uint8_t resp_val;
  uint16_t resp_msg;
};

struct VerilatorCtrl {
  // activates or deactivates tracing
  bool tracing_active;
};

struct GcdState {
  GcdInputs inputs;
  GcdOutputs outputs;
  // fields to control the verilator simulation
  VerilatorCtrl ctrl;
};

#endif  // SIMS_MISC_GCD_INCLUDE_GCD_VERILATOR_H_
