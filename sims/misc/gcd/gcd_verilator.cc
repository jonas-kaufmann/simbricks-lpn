
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
#include "include/gcd_verilator.h"

#include <Vgcd.h>
#include <signal.h>
#include <verilated.h>
#include <verilated_vcd_c.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <sstream>
#include <type_traits>

#include "simbricks/base/cxxatomicfix.h"
extern "C" {
#include "simbricks/pcie/if.h"
}

#include "simbricks/pcie/proto.h"

#define MMIO_DEBUG 0

namespace {
VerilatedContext vcontext{};
Vgcd top{&vcontext, "gcd"};
VerilatedVcdC trace{};

uint64_t clock_period = 10 * 1000ULL;  // 10ns -> 100MHz
int exiting;
int print_cur_ts_requested;
bool tracing_active;
char *trace_filename;
uint64_t trace_nr;
uint64_t tracing_start;  // used to make every trace start at 0
struct SimbricksPcieIf pcieif;

// Represents continuous memory mapped into BAR[0]. Gives host access to chip's
// inputs and outputs plus some control signals.
GcdState mapped_gcd_state{};

void chip_read_outputs(GcdState &state) {
  state.outputs.req_rdy = top.req_rdy;
  state.outputs.resp_val = top.resp_val;
  state.outputs.resp_msg = top.resp_msg;
}

void chip_write_inputs(GcdState &state) {
  top.req_val = state.inputs.req_val;
  top.reset = state.inputs.reset;
  top.resp_rdy = state.inputs.resp_rdy;
  top.req_msg = state.inputs.req_msg;
}

extern "C" void sigint_handler(int dummy) {
  exiting = 1;
}

extern "C" void sigusr1_handler(int dummy) {
  print_cur_ts_requested = 1;
}

volatile union SimbricksProtoPcieD2H *d2h_alloc(uint64_t cur_ts) {
  volatile union SimbricksProtoPcieD2H *msg;
  while (!(msg = SimbricksPcieIfD2HOutAlloc(&pcieif, cur_ts))) {
  }
  return msg;
}

bool PciIfInit(const char *shm_path,
               struct SimbricksBaseIfParams &baseif_params) {
  struct SimbricksBaseIfSHMPool pool;
  struct SimBricksBaseIfEstablishData ests;
  struct SimbricksProtoPcieDevIntro d_intro;
  struct SimbricksProtoPcieHostIntro h_intro;

  memset(&pool, 0, sizeof(pool));
  memset(&ests, 0, sizeof(ests));
  memset(&d_intro, 0, sizeof(d_intro));

  d_intro.pci_vendor_id = 0xdead;
  d_intro.pci_device_id = 0xbeef;
  d_intro.pci_class = 0x40;
  d_intro.pci_subclass = 0x00;
  d_intro.pci_revision = 0x00;

  static_assert(sizeof(GcdState) <= 1024, "GcdState too large for BAR");
  d_intro.bars[0].len = 1024;
  d_intro.bars[0].flags = SIMBRICKS_PROTO_PCIE_BAR_64;

  ests.base_if = &pcieif.base;
  ests.tx_intro = &d_intro;
  ests.tx_intro_len = sizeof(d_intro);
  ests.rx_intro = &h_intro;
  ests.rx_intro_len = sizeof(h_intro);

  if (SimbricksBaseIfInit(&pcieif.base, &baseif_params)) {
    std::cerr << "PciIfInit: SimbricksBaseIfInit failed\n";
    return false;
  }

  if (SimbricksBaseIfSHMPoolCreate(
          &pool, shm_path, SimbricksBaseIfSHMSize(&pcieif.base.params)) != 0) {
    std::cerr << "PciIfInit: SimbricksBaseIfSHMPoolCreate failed\n";
    return false;
  }

  if (SimbricksBaseIfListen(&pcieif.base, &pool) != 0) {
    std::cerr << "PciIfInit: SimbricksBaseIfListen failed\n";
    return false;
  }

  if (SimBricksBaseIfEstablish(&ests, 1)) {
    std::cerr << "PciIfInit: SimBricksBaseIfEstablish failed\n";
    return false;
  }

  return true;
}

// react to changes of ctrl signals
void apply_ctrl_changes() {
  // change to tracing_active
  if (!tracing_active && mapped_gcd_state.ctrl.tracing_active) {
    tracing_active = true;
    // tracing_start = vcontext.time();
    std::ostringstream trace_file{};
    trace_file << std::string{trace_filename} << "_" << trace_nr << ".vcd";
    std::cout << "trace_file: " << trace_file.str() << "\n";

    trace.open(trace_file.str().c_str());
    ++trace_nr;
  } else if (tracing_active && !mapped_gcd_state.ctrl.tracing_active) {
    tracing_active = false;
    trace.close();
  }
}

bool h2d_read(volatile struct SimbricksProtoPcieH2DRead &read,
              uint64_t cur_ts) {
  if (read.bar != 0) {
    std::cerr << "error: read from bar != 0\n";
    return false;
  }

  if (read.offset + read.len > sizeof(mapped_gcd_state)) {
    std::cerr << "error: h2d_read out of bounds\n";
    return false;
  }

#if MMIO_DEBUG
  std::cout << "got read cur_ts=" << cur_ts << std::hex << " offset=0x"
            << read.offset << " size=" << std::dec << read.len << std::endl;
#endif

  volatile union SimbricksProtoPcieD2H *msg = d2h_alloc(cur_ts);
  volatile struct SimbricksProtoPcieD2HReadcomp *readcomp;
  readcomp = &msg->readcomp;

  chip_read_outputs(mapped_gcd_state);

  if (read.offset < sizeof(mapped_gcd_state)) {
    uint8_t *gcd_state_array = reinterpret_cast<uint8_t *>(&mapped_gcd_state);
    std::copy(
        gcd_state_array + read.offset,
        gcd_state_array + std::min<uint64_t>(
                              sizeof(mapped_gcd_state) - read.offset, read.len),
        &readcomp->data[0]);
  }
  readcomp->req_id = read.req_id;

  SimbricksPcieIfD2HOutSend(&pcieif, msg,
                            SIMBRICKS_PROTO_PCIE_D2H_MSG_READCOMP);
  return true;
}

bool h2d_write(volatile struct SimbricksProtoPcieH2DWrite &write,
               uint64_t cur_ts, bool posted) {
  if (write.bar != 0) {
    std::cerr << "error: read from bar != 0\n";
    return false;
  }

  if (write.offset + write.len > sizeof(mapped_gcd_state)) {
    std::cerr << "warn: h2d_write out of bounds\n";
    return false;
  }

#if MMIO_DEBUG
  std::cout << "got write cur_ts=" << cur_ts << std::hex << " offset=0x"
            << write.offset << " size=" << std::dec << write.len << std::endl;
#endif

  // Only apply write to cur_in_out. The inputs of the chip are changed in the
  // main loop.
  if (write.offset < sizeof(mapped_gcd_state)) {
    uint8_t *gcd_state_array = reinterpret_cast<uint8_t *>(&mapped_gcd_state);
    std::copy(
        write.data,
        write.data + std::min<uint64_t>(sizeof(mapped_gcd_state) - write.offset,
                                        write.len),
        gcd_state_array);
  }

  apply_ctrl_changes();

  if (!posted) {
    volatile union SimbricksProtoPcieD2H *msg = d2h_alloc(cur_ts);
    volatile struct SimbricksProtoPcieD2HWritecomp &writecomp = msg->writecomp;
    writecomp.req_id = write.req_id;

    SimbricksPcieIfD2HOutSend(&pcieif, msg,
                              SIMBRICKS_PROTO_PCIE_D2H_MSG_WRITECOMP);
  }
  return true;
}

bool poll_h2d(uint64_t cur_ts) {
  volatile union SimbricksProtoPcieH2D *msg =
      SimbricksPcieIfH2DInPoll(&pcieif, cur_ts);

  // no msg available
  if (msg == nullptr)
    return true;

  uint8_t type = SimbricksPcieIfH2DInType(&pcieif, msg);

  switch (type) {
    case SIMBRICKS_PROTO_PCIE_H2D_MSG_READ:
      if (!h2d_read(msg->read, cur_ts)) {
        return false;
      }
      break;
    case SIMBRICKS_PROTO_PCIE_H2D_MSG_WRITE:
      if (!h2d_write(msg->write, cur_ts, false)) {
        return false;
      }
      break;
    case SIMBRICKS_PROTO_PCIE_H2D_MSG_WRITE_POSTED:
      if (!h2d_write(msg->write, cur_ts, true)) {
        return false;
      }
      break;
    case SIMBRICKS_PROTO_PCIE_H2D_MSG_DEVCTRL:
    case SIMBRICKS_PROTO_MSG_TYPE_SYNC:
      break; /* noop */
    case SIMBRICKS_PROTO_MSG_TYPE_TERMINATE:
      std::cerr << "poll_h2d: peer terminated\n";
      exiting = true;
      break;
    default:
      std::cerr << "warn: poll_h2d: unsupported type=" << type << "\n";
  }

  SimbricksPcieIfH2DInDone(&pcieif, msg);
  return true;
}

inline void print_cur_ts_if_requested(uint64_t cur_ts) {
  if (print_cur_ts_requested) {
    print_cur_ts_requested = 0;
    std::cout << "current timestamp " << cur_ts << std::endl;
  }
}
}  // namespace

int main(int argc, char **argv) {
  if (argc < 3 || argc > 8) {
    std::cerr << "Usage: gcd_verilator PCI-SOCKET SHM [START-TIMESTAMP-PS] "
                 "[SYNC-PERIOD] [PCI-LATENCY] [CLOCK-FREQ-MHZ] TRACE-FILE\n";
    return EXIT_FAILURE;
  }

  struct SimbricksBaseIfParams if_params;
  memset(&if_params, 0, sizeof(if_params));
  SimbricksPcieIfDefaultParams(&if_params);

  vcontext.time(strtoull(argv[3], nullptr, 0));
  if_params.sync_interval = strtoull(argv[4], nullptr, 0) * 1000ULL;
  if_params.link_latency = strtoull(argv[5], nullptr, 0) * 1000ULL;
  clock_period = 1000000ULL / strtoull(argv[6], nullptr, 0);

  if_params.sock_path = argv[1];
  if (!PciIfInit(argv[2], if_params)) {
    return EXIT_FAILURE;
  }

  bool sync = SimbricksBaseIfSyncEnabled(&pcieif.base);

  signal(SIGINT, sigint_handler);
  signal(SIGUSR1, sigusr1_handler);

  // setup tracing
  vcontext.traceEverOn(true);
  trace.dumpvars(1, "gcd");
  trace_filename = argv[7];
  top.trace(&trace, 1);

  // reset chip
  top.reset = 1;
  top.eval();
  top.clk = 1;
  top.eval();
  top.reset = 0;

  // main simulation loop
  while (!exiting) {
    uint64_t cur_ts = vcontext.time();
    print_cur_ts_if_requested(cur_ts);

    // send required sync messages
    while (SimbricksPcieIfD2HOutSync(&pcieif, cur_ts) < 0) {
      std::cerr << "warn: SimbricksPcieIfD2HOutSync failed cur_ts=" << cur_ts
                << std::endl;
    }

    print_cur_ts_if_requested(cur_ts);

    // process available incoming messages for current timestamp
    do {
      poll_h2d(cur_ts);
    } while (!exiting &&
             ((sync && SimbricksPcieIfH2DInTimestamp(&pcieif) <= cur_ts)));

    // falling edge
    top.clk = 0;
    top.eval();
    if (tracing_active) {
      trace.dump(cur_ts - tracing_start);
    }
    vcontext.timeInc(clock_period / 2);

    // apply possible changes to inputs to model
    chip_write_inputs(mapped_gcd_state);

    // evaluate Verilator model for one tick
    top.clk = 1;
    top.eval();
    if (tracing_active) {
      trace.dump(vcontext.time() - tracing_start);
    }
    vcontext.timeInc(clock_period / 2);
  }

  trace.close();
  top.final();
  return 0;
}
