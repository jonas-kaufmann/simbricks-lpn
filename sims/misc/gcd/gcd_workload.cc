
#include <linux/vfio.h>

#include <algorithm>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>

#include "gcd_verilator.h"
#include "vfio.h"

void hw_gcd(volatile GcdState &gcd_unit, const uint16_t first_int,
            const uint16_t second_int, const unsigned repetitions) {
  const uint16_t correct_result = std::gcd(first_int, second_int);

  std::cout << "hw_gcd: starting computation first_int=" << first_int
            << " second_int=" << second_int << " repetitions=" << repetitions
            << std::endl;

  gcd_unit.ctrl.tracing_active = true;
  for (unsigned i = 0; i < repetitions; ++i) {
    gcd_unit.inputs.req_msg = first_int << 16 | second_int;
    gcd_unit.inputs.req_val = 1;

    // busy wait until request ready
    while (gcd_unit.outputs.resp_val == 0) {
    }

    uint16_t response = gcd_unit.outputs.resp_msg;
    if (response != correct_result) {
      std::cerr << "GCD Chip produced " << response << " but correct result is "
                << correct_result << std::endl;
      throw;
    }

    gcd_unit.inputs.req_val = 0;
    gcd_unit.inputs.resp_rdy = 1;

    // busy wait until ready to receive next request
    while (gcd_unit.outputs.req_rdy == 0) {
    }

    gcd_unit.inputs.resp_rdy = 0;
  }
  gcd_unit.ctrl.tracing_active = false;

  std::cout << "hw_gcd: finished computation first_int=" << first_int
            << " second_int=" << second_int << " repetitions=" << repetitions
            << std::endl;
}

int main(int argc, char **argv) {
  if (argc < 3 || argc > 3) {
    std::cerr << "Usage: gcd_workload PCI-DEVICE WORKLOAD-TYPE\n";
    return EXIT_FAILURE;
  }
  int workload_type = std::stoi(argv[2]);

  int vfio_fd = vfio_init(argv[1]);
  if ((vfio_fd) < 0) {
    std::cerr << "vfio init failed" << std::endl;
    return 1;
  }

  size_t reg_len;
  void *mapped_addr;
  if (vfio_map_region(vfio_fd, VFIO_PCI_BAR0_REGION_INDEX, &mapped_addr,
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
  volatile GcdState &gcd_unit =
      *reinterpret_cast<volatile GcdState *>(mapped_addr);

  switch (workload_type) {
    case 0:
      // short computation workload: frequent input changes
      hw_gcd(gcd_unit, 6, 20, 1000);

      // long computation workload: infrequent input changes
      hw_gcd(gcd_unit, 977, /* max value */ -1, 10);
      break;
  }

  return 0;
}