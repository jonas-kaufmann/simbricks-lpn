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
#include "sims/lpn/vta/include/vta/io_gen.h"

#include "sims/lpn/vta/lpn_def/lpn_def.hh"


#define MASK5 0b11111
#define MASK6 0b111111

#define VTA_DEBUG 0

uint64_t in_flight_write = 0;

void KickSim(CtlVar& ctrl, int tag){
  std::unique_lock lk(ctrl.mx);
  if(ctrl.finished){
    return;
  }
  // Verify if wake up is nee
   if (ctrl.req_matcher[tag].isCompleted()) {
    if (ctrl.blocked) {
      // Notify to wake up
      ctrl.blocked = false;
      ctrl.cv.notify_one();
    }
    // Wait for func sim to process
    ctrl.cv.wait(lk, [&] { return ctrl.blocked || ctrl.finished; });
   }
}

void WaitForSim(CtlVar& ctrl){
  std::unique_lock lk(ctrl.mx);
  // Verify if wake up is needed
  if (ctrl.blocked) {
    // Notify to wake up
    ctrl.blocked = false;
    ctrl.cv.notify_one();
  }
  ctrl.cv.wait(lk, [&] { return ctrl.blocked || ctrl.finished; });
}

namespace {
VTABm vta_sim{};
VTADeviceHandle vta_func_device;
VTAIOGenHandle vta_io_generator;
std::thread io_generator_thread;
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

  std::cout << "VTABm::SetupIntro" << std::endl;
  
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

    std::cerr << "LAUNCHING IO GENERATOR THREAD " << std::endl;
    vta_io_generator = VTAIOGenAlloc();
    io_generator_thread = std::thread(VTAIOGenRun, vta_io_generator, insn_phy_addr, insn_count, 10000000);

    WaitForSim(ctl_iogen);

    // Start func simulator thread
    std::cerr << "LAUNCHING FUNC SIM THREAD " << std::endl;
    vta_func_device = VTADeviceAlloc();
    func_thread = std::thread(VTADeviceRun, vta_func_device, insn_phy_addr, insn_count, 10000000);
    WaitForSim(ctl_func);

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
    putData(dma_op->dma_addr, dma_op->len, dma_op->tag, dma_op->write, TimePs(), dma_op->data);
    #ifdef VTA_DEBUG_DMA
      std::cerr << "DMA Complete: " << dma_op->tag << " " << dma_op->dma_addr << " " << dma_op->len << std::endl;
    #endif
  }
  // handle response to DMA write request
  else {
    putData(dma_op->dma_addr, dma_op->len, dma_op->tag, dma_op->write, TimePs(), dma_op->data);
    in_flight_write--;
    #ifdef VTA_DEBUG_DMA
      std::cerr << "DMA Write Complete: " << dma_op->tag << " " << dma_op->dma_addr << " " << dma_op->len << std::endl;
    #endif
    // Process Write
    // lpn_req->acquired_len += dma_op->len;
  }


  // Run LPN to process received memory
  uint64_t next_ts = NextCommitTime(t_list, T_SIZE); 

  KickSim(ctl_iogen, dma_op->tag);
  KickSim(ctl_func, dma_op->tag);
  
  // Check for end condition
  if (in_flight_write == 0 && ctl_iogen.finished && ctl_func.finished && lpn_finished() && next_ts == lpn::LARGE) {
    std::cerr << "DMAcomplete: VTADeviceRun finished " << std::endl;
    func_thread.join();
    io_generator_thread.join();
    VTADeviceFree(vta_func_device);
    VTAIOGenFree(vta_io_generator);
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
  for (auto &kv : io_req_map) {
    if (kv.second.empty()) continue;
    for(auto &req : kv.second){
      if (req->issue == 0 || req->issue == 2){
        continue;
      }
      if (req->issue == 1){
        if(req->rw == WRITE_REQ){
          if (req->acquired_len != req->len){
            continue;
          }
        }
        req->issue = 2;
        auto total_bytes = req->len;
        auto sent_bytes = 0;
        while(total_bytes > 0){
          auto bytes_to_req = std::min<uint64_t>(total_bytes, DMA_BLOCK_SIZE);
          if (req->rw == READ_REQ) {
            auto dma_op = std::make_unique<VTADmaReadOp<DMA_BLOCK_SIZE>>(req->addr + sent_bytes, bytes_to_req, req->tag);
            #ifdef VTA_DEBUG_DMA
              std::cerr << "Issue DMA Read: " << req->tag << " " << req->addr + sent_bytes << " " << bytes_to_req << std::endl;
            #endif
            IssueDma(std::move(dma_op));
          } else {
            // reset the len to record for completion
            req->acquired_len = 0;
            auto dma_op = std::make_unique<VTADmaWriteOp>(req->addr + sent_bytes, bytes_to_req, req->tag);
            std::memcpy(dma_op->buffer, req->buffer, bytes_to_req);
            in_flight_write++;
            #ifdef VTA_DEBUG_DMA
              std::cerr << "Issue DMA Write: " << req->tag << " " << req->addr + sent_bytes << " " << bytes_to_req  << " is_write " << dma_op->write<< std::endl;
            #endif
            IssueDma(std::move(dma_op));
          }
          total_bytes -= bytes_to_req;
          sent_bytes += bytes_to_req;
        }
      }
    }
  }
}

void VTABm::ExecuteEvent(std::unique_ptr<pciebm::TimedEvent> evt) {
  // commit all transitions who can commit at evt.time
  // alternatively, commit transitions one by one.
  // UpdateClk(TimePs());‘
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
  if (in_flight_write == 0 && ctl_func.finished && ctl_iogen.finished && lpn_finished() && next_ts == lpn::LARGE) {
      std::cerr << "Size of ctrl_func " <<  ctl_func.req_matcher[STORE_ID].reqs.size() << std::endl;
      std::cerr << "VTADeviceRun finished " << std::endl;
      func_thread.join();
      io_generator_thread.join();
      VTADeviceFree(vta_func_device);
      VTAIOGenFree(vta_io_generator);
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

  // Issue requests enqueued by IOGen
  for (auto &kv : io_req_map) {
    if (kv.second.empty()) continue;
    for(auto &req : kv.second){
      if (req->issue == 0 || req->issue == 2){
        continue;
      }
      
      if(req->rw == WRITE_REQ){
          if (req->acquired_len != req->len){
            continue;
          }
      }

      if (req->issue == 1){
        
        req->issue = 2;
        auto total_bytes = req->len;
        auto sent_bytes = 0;
        while(total_bytes > 0){
          auto bytes_to_req = std::min<uint64_t>(total_bytes, DMA_BLOCK_SIZE);
          if (req->rw == READ_REQ) {
            auto dma_op = std::make_unique<VTADmaReadOp<DMA_BLOCK_SIZE>>(req->addr + sent_bytes, bytes_to_req, req->tag);
            #ifdef VTA_DEBUG_DMA
              std::cerr << "Issue DMA Read: " << req->tag << " " << req->addr + sent_bytes << " " << bytes_to_req << std::endl;
            #endif
            IssueDma(std::move(dma_op));
          } else {
            // reset the len to record for completion
            req->acquired_len = 0;
            auto dma_op = std::make_unique<VTADmaWriteOp>(req->addr + sent_bytes, bytes_to_req, req->tag);
            std::memcpy(dma_op->buffer, req->buffer, bytes_to_req);
            in_flight_write++;
            #ifdef VTA_DEBUG_DMA
              std::cerr << "Issue DMA Write: " << req->tag << " " << req->addr + sent_bytes << " " << bytes_to_req << std::endl;
            #endif
            IssueDma(std::move(dma_op));
          }
          total_bytes -= bytes_to_req;
          sent_bytes += bytes_to_req;
        }
      }
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
