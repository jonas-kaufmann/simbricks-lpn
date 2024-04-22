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

#include <simbricks/pciebm/pciebm.hh>

#include "sims/lpn/vta/include/vta_regs.hh"
#include "sims/lpn/lpn_common/place_transition.hh"
#include "sims/lpn/lpn_common/lpn_sim.hh"
#include "sims/lpn/vta/include/lpn_req_map.hh"
#include "sims/lpn/vta/include/vta/driver.h"

#include "sims/lpn/vta/lpn_def/lpn_def.hh"


#define FREQ_MHZ 150000000
#define FREQ_MHZ_NORMALIZED 150
#define MASK5 0b11111
#define MASK6 0b111111

#define VTA_DEBUG 0
#define NUM_INSN 142

namespace {
VTABm vta_sim{};
VTADeviceHandle vta_func_device;
std::thread func_thread;

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
//   lpn_init();
  vta_func_device = VTADeviceAlloc();
  auto ids = std::vector<int>{LOAD_INSN, LOAD_INP_ID, LOAD_WGT_ID, LOAD_ACC_ID, LOAD_UOP_ID, STORE_ID};
  setupBufferMap(ids);
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
  
  uint32_t start = (Registers_.status & 0x1) == 0x1;
  if (start){
    uint64_t insn_phy_addr = Registers_.insn_phy_addr_hh; 
    insn_phy_addr = insn_phy_addr << 32 | Registers_.insn_phy_addr_lh;
    uint32_t insn_count = NUM_INSN;//Registers_.insn_count;
    std::cout << "insn_phy_addr: " << insn_phy_addr << " insn_count: " << insn_count << std::endl;

    // Start func simulator thread
    std::cout << "LAUNCHING FUNC SIM THREAD " << std::endl;
    func_thread = std::thread(VTADeviceRun, vta_func_device, insn_phy_addr, insn_count, 10000000);

    std::cout << "LAUNCHING LPN" << std::endl;
    lpn_start(insn_phy_addr, insn_count, sizeof(VTAGenericInsn));

    // Start simulating the LPN immediately
    auto evt = std::make_unique<pciebm::TimedEvent>();
    evt->time = TimePs();
    evt->priority = 0;
    EventSchedule(std::move(evt));
  }

  // VTADeviceRun starts the LPN because it models only a smaller portion

  // TODO Func sim should not be the one running the LPN and placing its tokens
  // TODO why LPN change doesn't recompile? How to recompile?

  // Problem: 
  // Func sim is the one placing the tokens into the LPN
  // Func Sim waits on LPN to issue the requests
  // No tokens are in the LPN, it blocks
  // Deadlock

  // Need logic inside func sim for LPN to parse tokens
  // LPN calls func sim functions to produce the tokens
}

// Matches the LPN requests with the corresponding DRAM requests used by func sim
void Match(int tag) {
  auto& dreq = frontReq(dram_req_map[tag]);
  if (!dram_req_map[tag].empty()) {
    // std::cerr << lpn_served_req_map[tag].size() <<" Try to match: " << tag << " addr" << dreq->addr << " len: " << dreq->len << " acquired_len: " << dreq->acquired_len << std::endl;
    // clean up the served req map
    while(1){
      auto& front = frontReq(lpn_served_req_map[tag]);
      if ((!lpn_served_req_map[tag].empty()) && front->consumed >= front->len){
        assert(front->consumed == front->len);
        std::cerr << "Dequeue LPN data: " << front->id << front->addr <<front->len << std::endl;
        dequeueReq(lpn_served_req_map[tag]);
      }else{
        break;
      }
    }
    if (lpn_served_req_map[tag].empty()) return;
    auto& front = frontReq(lpn_served_req_map[tag]);
    std::cerr << "Serve LPN data to func sim: " << front->id << front->addr <<front->len << std::endl;
    for (auto& lpn_req : lpn_served_req_map[tag]) {
      if (lpn_req->consumed == lpn_req->len) {
          continue;
      }
      // Check address bounds
      if ((lpn_req->addr + lpn_req->len) < dreq->addr || lpn_req->addr > (dreq->addr + dreq->len)){
        std::cerr << "no match " << lpn_req->addr << " " << lpn_req->len << " " << dreq->addr << " " << dreq->len << std::endl;
        continue;
      }
      uint32_t offset_into_dreq = 0;
      uint32_t offset_into_lpn_req = 0;
      uint32_t len = 0;
      auto lpn_start = lpn_req->addr + lpn_req->consumed;
      auto lpn_end = lpn_req->addr + lpn_req->len;
      auto fsim_start = dreq->addr + dreq->acquired_len;
      auto fsim_end = dreq->addr + dreq->len;
      if (lpn_start <= fsim_start) {
        len = std::min(lpn_end, fsim_end) - fsim_start;
        offset_into_dreq = 0;
        offset_into_lpn_req = fsim_start - lpn_start;
      } else {
        len = std::min(lpn_end, fsim_end) - lpn_start;
        offset_into_dreq = lpn_start - fsim_start;
        offset_into_lpn_req = 0;
      }
      memcpy(dreq->buffer + dreq->acquired_len+offset_into_dreq, lpn_req->buffer + lpn_req->consumed + offset_into_lpn_req, len);
      dreq->acquired_len += len;
      std::cerr << "Matched: " << tag << " len: " << dreq->len << " acquired_len: " << dreq->acquired_len << std::endl;
      lpn_req->consumed += len; 
      std::cerr << "Consumed " << len << " from LPN req: " << lpn_req->id <<"total:" << lpn_req->len << " consumed: " << lpn_req->consumed << std::endl;
      std::cerr << "remaining lpnreq" << lpn_served_req_map[tag].size() << std::endl;
    }
  } 
}

void MatchWrite(int tag) {
  auto& lpn_req = frontReq(lpn_req_map[tag]);
  if (!lpn_req_map[tag].empty()) {
    for (auto& dreq : dram_req_map[tag]) {

      if (dreq->acquired_len == dreq->len){
        // consumed all
        continue;
      }
      // no intersection
      if ((lpn_req->addr + lpn_req->len) < dreq->addr || lpn_req->addr > (dreq->addr + dreq->len))
        continue;

      // Copy buffer from LPN req into DRAM req 
      uint32_t offset_into_dreq = 0;
      uint32_t offset_into_lpn_req = 0;
      uint32_t len = 0;
      if (lpn_req->addr < dreq->addr) {
        len = std::max(lpn_req->addr + lpn_req->len, dreq->addr + dreq->len) - dreq->addr;
        offset_into_dreq = 0;
        offset_into_lpn_req = dreq->addr - lpn_req->addr;
      } else if (lpn_req->addr > dreq->addr) {
        len = std::max(lpn_req->addr + lpn_req->len, dreq->addr + dreq->len) - lpn_req->addr;
        offset_into_dreq = lpn_req->addr - dreq->addr;
        offset_into_lpn_req = 0;
      }
      memcpy(lpn_req->buffer + offset_into_lpn_req, dreq->buffer + offset_into_dreq, len);
      dreq->acquired_len += len;
      // consume from dreq
      lpn_req->consumed += len; 
    }
  } 
}
// Algo For Read
// 1. Process Read, Match into DRAM buffer
// 2. Notify FuncSim of new data, let it run until blocked
// 3. Run LPN to see if Load has completed, schedule next event 
// 4. Issue DMA ops from the previous step
void VTABm::DmaComplete(std::unique_ptr<pciebm::DMAOp> dma_op) {
  
  UpdateClk(t_list, T_SIZE, TimePs());
  // handle response to DMA read request
  uint32_t tag;
  int rw;
  if (!dma_op->write) {

    // Record read 
    tag = dma_op->tag;
    auto& req = frontReq(lpn_req_map[tag]);
    req->inflight = false;
    memcpy(req->buffer + req->acquired_len, dma_op->data, dma_op->len);   // TODO verify arithmetic
    req->acquired_len += dma_op->len;
    rw = 0;
    std::cout <<"Required: " << req->len << ", Acquired: " << req->acquired_len <<std::endl;
    // Matches LPN request with DRAM request
  }
  // handle response to DMA write request
  else {
    assert(0);
    // TODO handle writes
    tag = dma_op->tag;
    rw = 1;
    auto& lpn_req = frontReq(lpn_req_map[tag]);
    if (!lpn_req_map[tag].empty()){
      lpn_req->inflight = false;    
      lpn_req->acquired_len += dma_op->len;
      std::cerr << "DMA write completed" <<" acquired len:" << lpn_req->acquired_len << "len" << dma_op->len << std::endl;

    }else{
      assert(0);
    }
  }

  // TODO this indicates func sim has finished, not that write has completed?
  // Finished condition should take into account both ?
  // What if last operation is a write, func sim completes, but not dma yet?
  if (finished) {
    std::cerr << "VTADeviceRun finished " << std::endl;
    Registers_.status = 0x4;
  }
  
  // only starts lpn after the func sim is done
  // Executes delay functions which can fire
  // if(Registers_.status == 0x4){
  // let lpn figure out what to unblock
  uint64_t next_ts = NextCommitTime(t_list, T_SIZE); 

  if (rw == 0){
    Match(tag);
  }
  // Notify funcsim
  {
    std::unique_lock lk(m_proc);
    if (sim_blocked) {
      // Notify to wake up
      std::cerr << "wake up func_sim.cc in DmaComplete" << std::endl;  
      sim_blocked = false;
      cv.notify_one();
      cv.wait(lk, [] { return sim_blocked || finished;});
    }else{
      lk.unlock();
    }
    // Wait for func sim to process
  }
  std::cerr << "return to vta_bm.cc in DmaComplete" << std::endl;  

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
      if( next_ts == lpn::LARGE && Registers_.status == 0x4 && !next_scheduled ){
        Registers_.status = 0x2;
        TransitionCountLog(t_list, T_SIZE);
        return;
      }

  // Issue requests which have been schedueld by LPN
  for (auto &kv : lpn_req_map) {
    auto &req = frontReq(kv.second);
    if (kv.second.empty()) continue;
    if (req->inflight == false) {
      if(req->len == req->acquired_len) continue;
      auto bytes_to_req = std::min<uint64_t>(req->len - req->acquired_len, DMA_BLOCK_SIZE);
      if(req->rw == READ_REQ){
        auto dma_op = std::make_unique<VTADmaReadOp<DMA_BLOCK_SIZE>>(req->addr+req->acquired_len, bytes_to_req, req->id);
        IssueDma(std::move(dma_op));
      }
      else{
        // MatchWrite(kv.first);
        // if (req->consumed > req->acquired_len) {
              // auto bytes_to_req = std::min<uint64_t>(req->consumed - req->acquired_len, DMA_BLOCK_SIZE);
        if (req->len > req->acquired_len) {
          // consumed more from the dram req
          std::cerr << "Write req: " << req->id << " len: " << req->len << " acquired_len: " << req->acquired_len << std::endl;
          auto bytes_to_req = std::min<uint64_t>(req->len - req->acquired_len, DMA_BLOCK_SIZE);
          auto dma_op = std::make_unique<VTADmaWriteOp>(req->addr+req->acquired_len, bytes_to_req, req->id);
          IssueDma(std::move(dma_op));
          std::cerr << "Write req issued " << std::endl;
        }
      }
      req->inflight = true;
    }
  }
  // }
}

void VTABm::ExecuteEvent(std::unique_ptr<pciebm::TimedEvent> evt) {
  // commit all transitions who can commit at evt.time
  // alternatively, commit transitions one by one.
  // UpdateClk(TimePs());‘
  std::cerr << "ExecuteEvent: " << evt->time << std::endl;
  uint64_t next_ts = lpn::LARGE;
  auto inplen = lpn_served_req_map[LOAD_INP_ID].size();
  auto wgtlen = lpn_served_req_map[LOAD_WGT_ID].size();
  auto acclen = lpn_served_req_map[LOAD_ACC_ID].size();
  auto uoplen = lpn_served_req_map[LOAD_UOP_ID].size();
  while(1){
    CommitAtTime(t_list, T_SIZE, evt->time);
    // TransitionCountLog(t_list, T_SIZE);
    next_ts = NextCommitTime(t_list, T_SIZE);
    assert(inplen == lpn_served_req_map[LOAD_INP_ID].size());
    assert(wgtlen == lpn_served_req_map[LOAD_WGT_ID].size());
    assert(acclen == lpn_served_req_map[LOAD_ACC_ID].size());
    assert(uoplen == lpn_served_req_map[LOAD_UOP_ID].size());
    if (next_ts > evt->time) break;
  }

  // iterate through load tags
  for (int i = 0; i < 4; i++) {
    Match(i);
  }
  // Notify funcsim
  {
    std::unique_lock lk(m_proc);
    if (sim_blocked) {
      std::cerr << "wake up func_sim.cc" << std::endl;
      // Notify to wake up
      sim_blocked = false;
      cv.notify_one();
      cv.wait(lk, [] { return sim_blocked || finished; });
    }else{
      lk.unlock();
    }
    // Wait for func sim to process
  }
  std::cerr << "return to vta_bm.cc" << std::endl;
  if (next_ts == lpn::LARGE && Registers_.status == 0x4) {
    Registers_.status = 0x2;
    TransitionCountLog(t_list, T_SIZE);
    return;
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

  // TODO not needed?? finished can only be updated after wrapper wakes up func sim
  if(finished){
      std::cerr << "VTADeviceRun finished " << std::endl;
      Registers_.status = 0x4;
  }

  // Issue requests enqueued by LPN
  for (auto &kv : lpn_req_map) {
    auto &req = frontReq(kv.second);
    if (kv.second.empty()) continue;
    if (req->inflight == false) {
      if (req->len == req->acquired_len){
        std::cerr << "Req already acquired" << req->id << " " << req->addr << " " << req->len << std::endl;
        continue;
      } 
      auto bytes_to_req = std::min<uint64_t>(req->len - req->acquired_len, DMA_BLOCK_SIZE);
      if (req->rw == READ_REQ) {
        auto dma_op = std::make_unique<VTADmaReadOp<DMA_BLOCK_SIZE>>(req->addr + req->acquired_len, bytes_to_req, req->id);
        IssueDma(std::move(dma_op));
      } else {
        auto dma_op = std::make_unique<VTADmaWriteOp>(req->addr + req->acquired_len, bytes_to_req, req->id);
        auto write_head = write_buffer_map[req->id]->getHead();
        std::memcpy(dma_op->buffer, write_head, bytes_to_req);
        IssueDma(std::move(dma_op));
      }
      req->inflight = true;
    }
  }
  if (next_ts == lpn::LARGE && Registers_.status == 0x4 && !next_scheduled) {
    Registers_.status = 0x2;
    TransitionCountLog(t_list, T_SIZE);
    return;
  }
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
