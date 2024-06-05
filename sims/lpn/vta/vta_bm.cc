#include "include/vta_bm.hh"
#include "include/vta/hw_spec.h"

#include <bits/stdint-uintn.h>
#include <bits/types/siginfo_t.h>
#include <signal.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <sys/time.h>

#include <simbricks/pciebm/pciebm.hh>

#include "sims/lpn/vta/include/vta_regs.hh"
#include "sims/lpn/lpn_common/place_transition.hh"
#include "sims/lpn/lpn_common/lpn_sim.hh"
#include "sims/lpn/vta/include/lpn_req_map.hh"
#include "sims/lpn/vta/include/vta/driver.h"

#include "sims/lpn/vta/lpn_def/lpn_def.hh"


#define MASK5 0b11111
#define MASK6 0b111111

#define VTA_DEBUG 0
#define NUM_INSN 142

namespace {
VTABm vta_sim{};
VTADeviceHandle vta_func_device;
std::thread func_thread;
double start_time;
std::vector<int> ids = {LOAD_INSN, LOAD_INP_ID, LOAD_WGT_ID, LOAD_ACC_ID, LOAD_UOP_ID, STORE_ID};

void sigint_handler(int dummy) {
  vta_sim.SIGINTHandler();
}

void sigusr1_handler(int dummy) {
  vta_sim.SIGUSR1Handler();
}

void sigusr2_handler(int dummy) {
  vta_sim.SIGUSR2Handler();
}

}  // namespace

void VTABm::SetupIntro(struct SimbricksProtoPcieDevIntro &dev_intro) {
  dev_intro.pci_vendor_id = 0xdead;
  dev_intro.pci_device_id = 0xbeef;
  dev_intro.pci_class = 0x40;
  dev_intro.pci_subclass = 0x00;
  dev_intro.pci_revision = 0x00;

  // request one BAR
  static_assert(sizeof(VTARegs) <= 4096, "Registers don't fit BAR");
  dev_intro.bars[0].len = 4096;
  dev_intro.bars[0].flags = 0;

  // setup LPN initial state
  // auto ids = std::vector<int>{LOAD_INSN, LOAD_INP_ID, LOAD_WGT_ID, LOAD_ACC_ID, LOAD_UOP_ID, STORE_ID};
  setupReqQueues(ids);
  lpn_init();
}

void VTABm::RegRead(uint8_t bar, uint64_t addr, void *dest,
                            size_t len) {
  if (bar != 0) {
    std::cerr << "error: register read from unmapped BAR " << bar << "\n";
    return;
  }
  if (addr + len > sizeof(Registers_)) {
    std::cerr << "error: register read is outside bounds offset=" << addr
              << " len=" << len << "\n";
    return;
  }

  std::memcpy(dest, reinterpret_cast<uint8_t *>(&Registers_) + addr, len);
}


void VTABm::RegWrite(uint8_t bar, uint64_t addr, const void *src,
                             size_t len) {
  if (bar != 0) {
    std::cerr << "error: register write to unmapped BAR " << bar << "\n";
    return;
  }
  if (addr + len > sizeof(Registers_)) {
    std::cerr << "error: register write is outside bounds offset=" << addr
              << " len=" << len << "\n";
    return;
  }

  std::cerr << "Register write offset=" << addr
              << " len=" << len << " value=" << (*((uint32_t*)(src))) <<"\n";
  std::memcpy(reinterpret_cast<uint8_t *>(&Registers_) + addr, src, len);
  
  if (addr == 20 && (Registers_._0x14 & 0x1) == 0x1) {
    std::cerr << "Resetting vtabm and lpn" << std::endl;
    lpn_reset();
    std::memset(reinterpret_cast<uint8_t *>(&Registers_), 0, 36);
    return;
  }

  uint32_t start = (Registers_.status & 0x1) == 0x1;
  if (start && lpn_started==false){

    uint64_t insn_phy_addr = Registers_.insn_phy_addr_hh; 
    insn_phy_addr = insn_phy_addr << 32 | Registers_.insn_phy_addr_lh;
    uint32_t insn_count = Registers_.insn_count;
    std::cout << "insn_phy_addr: " << insn_phy_addr << " insn_count: " << insn_count << std::endl;

    struct timeval tp;
    gettimeofday(&tp, NULL);
    start_time = double(tp.tv_sec) + tp.tv_usec / double(1000000);

    // Start func simulator thread
    std::cerr << "LAUNCHING FUNC SIM THREAD " << std::endl;
    vta_func_device = VTADeviceAlloc();
    func_thread = std::thread(VTADeviceRun, vta_func_device, insn_phy_addr, insn_count, 10000000);

    // Wait for func sim to register first request
    {
      std::unique_lock lk(m_proc);
      // Verify if wake up is needed
      if (sim_blocked) {
        // Notify to wake up
        sim_blocked = false;
        cv.notify_one();
      }
      // Wait for func sim to process
      cv.wait(lk, [] { return sim_blocked || finished; });
    }

    num_instr = insn_count;
    std::cerr << "LAUNCHING LPN with insns: " << num_instr << std::endl;
    lpn_start(insn_phy_addr, insn_count, sizeof(VTAGenericInsn));

    // Start simulating the LPN immediately
    auto evt = std::make_unique<pciebm::TimedEvent>();
    evt->time = TimePs();
    evt->priority = 0;
    EventSchedule(std::move(evt));
  }
}

// Algo For Read
// 1. Process Read, put into lpn_req buffer
// 2. Run LPN to produce mem requests
// 3. Notify func sim
// 4. Issue new DMA ops 
void VTABm::DmaComplete(std::unique_ptr<pciebm::DMAOp> dma_op) {
  
  UpdateClk(t_list, T_SIZE, TimePs());
  // handle response to DMA read request
  if (!dma_op->write) {

    // Process read 
    auto& req = frontReq(lpn_req_map[dma_op->tag]);
    req->inflight = false;
    memcpy(req->buffer + req->acquired_len, dma_op->data, dma_op->len);   // TODO verify arithmetic
    req->acquired_len += dma_op->len;
    // std::cerr << "DMA Complete: " << dma_op->tag << " " << req->acquired_len << " " << req->len << std::endl;

  }
  // handle response to DMA write request
  else {

    // Process Write
    auto& lpn_req = frontReq(lpn_req_map[dma_op->tag]);
    lpn_req->inflight = false;
    lpn_req->acquired_len += dma_op->len;
  }


  // Run LPN to process received memory
  uint64_t next_ts = NextCommitTime(t_list, T_SIZE); 

  auto& matcher = func_req_map[dma_op->tag];
  // Notify funcsim
  {
    std::unique_lock lk(m_proc);
    // Verify if wake up is needed
    if (matcher.isCompleted()) {
      if (sim_blocked) {
        // Notify to wake up
        sim_blocked = false;
        cv.notify_one();
      }
      // Wait for func sim to process
      cv.wait(lk, [] { return sim_blocked || finished; });
    }
  }

  // Check for end condition
  if (finished && lpn_finished() && next_ts == lpn::LARGE) {
    std::cerr << "DMAcomplete: VTADeviceRun finished " << std::endl;
    func_thread.join();
    VTADeviceFree(vta_func_device);
    lpn_end();
    ClearReqQueues(ids);

    struct timeval tp;
    gettimeofday(&tp, NULL);
    double end = double(tp.tv_sec) + (tp.tv_usec / double(1000000));
    std::cerr << "EXECUTION TIME: " << (end - start_time) << " seconds" << std::endl;

    Registers_.status = 0x2;
    TransitionCountLog(t_list, T_SIZE);
    return ;
  }

  // Schedule next event
  #if VTA_DEBUG
      std::cerr << "next_ts=" << next_ts <<  " TimePs=" << TimePs() << " lpnLarge=" << lpn::LARGE << "\n";
  #endif
      assert(next_ts >= TimePs() &&
             "VTABm::DmaComplete: Cannot schedule event for past timestamp");
      auto next_scheduled = EventNext();
      if (next_ts != lpn::LARGE &&
          (!next_scheduled || next_scheduled.value() > next_ts)) {
  #if VTA_DEBUG
        std::cerr << "schedule next at = " << next_ts << "\n";
  #endif
        auto evt = std::make_unique<pciebm::TimedEvent>();
        evt->time = next_ts;
        EventSchedule(std::move(evt));
      }

  // Issue requests enqueued by LPN
  for (auto &kv : lpn_req_map) {
    auto &req = frontReq(kv.second);
    if (kv.second.empty()) continue;
    if (req->inflight == false) {
      if (req->len == req->acquired_len) continue;
      auto bytes_to_req = std::min<uint64_t>(req->len - req->acquired_len, DMA_BLOCK_SIZE);
      if (req->rw == READ_REQ) {
        auto dma_op = std::make_unique<VTADmaReadOp<DMA_BLOCK_SIZE>>(req->addr + req->acquired_len, bytes_to_req, req->id);
        IssueDma(std::move(dma_op));
      } else {
        auto dma_op = std::make_unique<VTADmaWriteOp>(req->addr + req->acquired_len, bytes_to_req, req->id);
        std::memcpy(dma_op->buffer, req->buffer, bytes_to_req);
        IssueDma(std::move(dma_op));
      }
      req->inflight = true;
    }
  }
}

void VTABm::ExecuteEvent(std::unique_ptr<pciebm::TimedEvent> evt) {
  // commit all transitions who can commit at evt.time
  // alternatively, commit transitions one by one.
  // UpdateClk(TimePs());‘
  //std::cerr << "ExecuteEvent: " << evt->time << std::endl;
  uint64_t next_ts = lpn::LARGE;
  while(1){
    CommitAtTime(t_list, T_SIZE, evt->time);
    // TransitionCountLog(t_list, T_SIZE);
    next_ts = NextCommitTime(t_list, T_SIZE);
    if (next_ts > evt->time) break;
  }

#if VTA_DEBUG
  std::cerr << "lpn exec: evt time=" << evt->time << " TimePs=" << TimePs()
            << " next_ts=" << next_ts <<  " lpnLarge=" << lpn::LARGE << "\n";
#endif
  // only schedule an event if one doesn't exist yet
  assert(next_ts >= TimePs() &&
      "VTABm::ExecuteEvent: Cannot schedule event for past timestamp");
  auto next_scheduled = EventNext();

  if (next_ts != lpn::LARGE &&
      (!next_scheduled || next_scheduled.value() > next_ts)) {
#if VTA_DEBUG
    std::cerr << "schedule next at = " << next_ts << "\n";
#endif
    evt->time = next_ts;
    evt->priority = 0;
    EventSchedule(std::move(evt));
  }

  uint64_t insn_phy_addr = Registers_.insn_phy_addr_hh; 
  insn_phy_addr = insn_phy_addr << 32 | Registers_.insn_phy_addr_lh;
  // uint32_t insn_count = NUM_INSN;//Registers_.insn_count;
  // std::cerr << "insn_phy_addr: " << insn_phy_addr << " insn_count: " << insn_count << std::endl;
  // std::cerr << "Remaining insns " << num_instr << std::endl;
  if (finished && lpn_finished() && next_ts == lpn::LARGE) {
      std::cerr << "VTADeviceRun finished " << std::endl;
      func_thread.join();
      VTADeviceFree(vta_func_device);
      ClearReqQueues(ids);
      lpn_end();

      struct timeval tp;
      gettimeofday(&tp, NULL);
      double end = double(tp.tv_sec) + (tp.tv_usec / double(1000000));
      std::cerr << "EXECUTION TIME: " << (end - start_time) << " seconds" << std::endl;

      Registers_.status = 0x2;
      TransitionCountLog(t_list, T_SIZE);
      return;
  }

  // Issue requests enqueued by LPN
  for (auto &kv : lpn_req_map) {
    auto &req = frontReq(kv.second);
    if (kv.second.empty()) continue;
    if (req->inflight == false) {
      if (req->len == req->acquired_len) continue;
      auto bytes_to_req = std::min<uint64_t>(req->len - req->acquired_len, DMA_BLOCK_SIZE);
      if (req->rw == READ_REQ) {
        auto dma_op = std::make_unique<VTADmaReadOp<DMA_BLOCK_SIZE>>(req->addr + req->acquired_len, bytes_to_req, req->id);
        IssueDma(std::move(dma_op));
      } else {
        auto dma_op = std::make_unique<VTADmaWriteOp>(req->addr + req->acquired_len, bytes_to_req, req->id);
        std::memcpy(dma_op->buffer, req->buffer, bytes_to_req);
        IssueDma(std::move(dma_op));
      }
      req->inflight = true;
    }
  }
  // if (next_ts == lpn::LARGE && Registers_.status == 0x4 && !next_scheduled) {
  //   Registers_.status = 0x2;
  //   TransitionCountLog(t_list, T_SIZE);
  //   return;
  // }
}

void VTABm::DevctrlUpdate(
    struct SimbricksProtoPcieH2DDevctrl &devctrl) {
  // ignore this for now
  std::cerr << "warning: ignoring SimBricks DevCtrl message with flags "
            << devctrl.flags << "\n";
}

int main(int argc, char *argv[]) {
  signal(SIGINT, sigint_handler);
  signal(SIGUSR1, sigusr1_handler);
  signal(SIGUSR2, sigusr2_handler);
  if (!vta_sim.ParseArgs(argc, argv)) {
    return EXIT_FAILURE;
  }
  return vta_sim.RunMain();
}
