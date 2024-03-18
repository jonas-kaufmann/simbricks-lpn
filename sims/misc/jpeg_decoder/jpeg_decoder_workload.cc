#include <fcntl.h>
#include <linux/vfio.h>
#include <sys/mman.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <ratio>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "include/jpeg_decoder_regs.hh"
#include "include/vfio.h"

#define DEBUG 0

int main(int argc, char *argv[]) {
  if (argc != 5) {
    std::cerr << "usage: jpeg_decoder_workload PCI-DEVICE DMA-SRC DMA-SRC-LEN "
                 "DMA-DST\n";
    return EXIT_FAILURE;
  }

  int vfio_fd = vfio_init(argv[1]);
  if (vfio_fd < 0) {
    std::cerr << "vfio init failed" << std::endl;
    return 1;
  }

  void *bar0;
  size_t reg_len;
  if (vfio_map_region(vfio_fd, 0, &bar0, &reg_len)) {
    std::cerr << "vfio_map_region for bar 0 failed" << std::endl;
    return 1;
  }
  void *bar1;
  if (vfio_map_region(vfio_fd, 1, &bar1, &reg_len)) {
    std::cerr << "vfio_map_region for bar 1 failed" << std::endl;
    return 1;
  }
  void *bar2;
  if (vfio_map_region(vfio_fd, 2, &bar2, &reg_len)) {
    std::cerr << "vfio_map_region for bar 2 failed" << std::endl;
    return 1;
  }

  // required for DMA
  if (vfio_busmaster_enable(vfio_fd)) {
    std::cerr << "vfio busmiaster enable faled" << std::endl;
    return 1;
  }

  volatile VerilatorRegs &verilator_regs = *(volatile VerilatorRegs *)bar0;
  volatile JpegDecoderRegs &jpeg_decoder_regs =
      *(volatile JpegDecoderRegs *)bar1;
  volatile ClockGaterRegs &clock_gater_regs = *(volatile ClockGaterRegs *)bar2;

  if (jpeg_decoder_regs.isBusy) {
    std::cerr << "error: jpeg decoder is unexpectedly busy\n";
    return 1;
  }

  uintptr_t dma_src_addr = std::stoul(argv[2], nullptr, 0);
  uint32_t dma_src_len = std::stoul(argv[3], nullptr, 0);
  uintptr_t dma_dst_addr = std::stoul(argv[4], nullptr, 0);

  // activate jpeg decoder and tracing
  // clock_gater_regs.clock_1_active = 1;

  // submit image to decode
  std::cout << "info: submitting image to jpeg decoder\n";
  jpeg_decoder_regs.src = dma_src_addr;
  jpeg_decoder_regs.dst = dma_dst_addr;
  std::chrono::steady_clock::time_point begin =
      std::chrono::steady_clock::now();
  verilator_regs.tracing_active = true;
  jpeg_decoder_regs.ctrl = dma_src_len | 0x80000000;  // set bit 31 to 1

  // verify values written to registers
  // if (jpeg_decoder_regs.src != dma_src_addr ||
  //     jpeg_decoder_regs.dst != dma_dst_addr ||
  //     jpeg_decoder_regs.ctrl != dma_src_len) {
  //   std::cerr
  //       << "error: values written to registers don't match expectations\n";
  //   return 1;
  // }

  // wait until decoding finished
  while (true) {
    if (!jpeg_decoder_regs.isBusy) {
      break;
    }
    // std::this_thread::sleep_for(std::chrono::microseconds{1});
  }

  // report duration
  verilator_regs.tracing_active = false;
  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
  std::cout << "duration: "
            << std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin)
                   .count()
            << " ns\n";

  // produce new trace file for jpeg decoder idle
  std::cout << "info: idle jpeg decoder\n";
  verilator_regs.tracing_active = true;
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  verilator_regs.tracing_active = false;

  // produce new trace file for jpeg decoder clock gated
  std::cout << "info: clock gated jpeg decoder\n";
  clock_gater_regs.clock_1_active = 0;
  verilator_regs.tracing_active = true;
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  verilator_regs.tracing_active = false;

  return 0;
}