#pragma once
#include <cstdint>

// control registers for the JPEG decoder block
struct __attribute__((packed)) JpegDecoderRegs {
  uint32_t ctrl;
  uint32_t isBusy;
  uint32_t src;
  uint32_t dst;
};

struct __attribute__((packed)) VerilatorRegs {
  // activates or deactivates tracing
  bool tracing_active;
};

struct __attribute__((packed)) ClockGaterRegs {
  uint32_t clock_1_active;
  uint32_t clock_2_active;
  uint32_t clock_3_active;
  uint32_t clock_4_active;
};
