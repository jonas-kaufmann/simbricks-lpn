#include "include/vta_bm.hh"

#include <bits/stdint-uintn.h>
#include <bits/types/siginfo_t.h>
#include <signal.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

#include <simbricks/pciebm/pciebm.hh>

#include "sims/lpn/vta/include/vta_regs.hh"
#include "sims/lpn/vta/include/vta/driver.h"
#include "sims/lpn/lpn_common/place_transition.hh"
#include "sims/lpn/lpn_common/lpn_sim.hh"
#include "sims/lpn/vta/include/lpn_req_map.hh"

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

  uint32_t old_is_busy = Registers_.status != 0;

  std::cerr << "Register write offset=" << addr
              << " len=" << len << " value=" << (*((uint32_t*)(src))) <<"\n";
  std::memcpy(reinterpret_cast<uint8_t *>(&Registers_) + addr, src, len);
  
  uint32_t start = (Registers_.status & 0x1) == 0x1;
  if (start){
    uint64_t insn_phy_addr = Registers_.insn_phy_addr_hh; 
    insn_phy_addr = insn_phy_addr << 32 | Registers_.insn_phy_addr_lh;
    uint32_t insn_count = NUM_INSN;//Registers_.insn_count;

    std::cerr << "insn_phy_addr: " << insn_phy_addr << " insn_count: " << insn_count << std::endl;
    VTADeviceRun(vta_func_device, insn_phy_addr, insn_count, 10000000);
  }

  // auto& lpn_req = frontReq<LpnReq>(lpn_req_map[LOAD_INSN]);
  // if (lpn_req == nullptr) return;

  auto& req = frontReq<DramReq>(dram_req_map[LOAD_INSN]);
  if (req == nullptr) return;
  auto bytes_to_req = std::min<uint64_t>(req->len - req->acquired_len, DMA_BLOCK_SIZE);
  if(req->rw == READ_REQ){
    auto dma_op = std::make_unique<VTADmaReadOp<DMA_BLOCK_SIZE>>(req->addr+req->acquired_len, bytes_to_req, req->id);
    IssueDma(std::move(dma_op));
  }

}

void VTABm::DmaComplete(std::unique_ptr<pciebm::DMAOp> dma_op) {
  
  UpdateClk(t_list, T_SIZE, TimePs());
  // handle response to DMA read request
  if (!dma_op->write) {
    // record the data read
    uint32_t tag = dma_op->tag;
    auto& req = frontReq(dram_req_map[tag]);
    req->inflight = false;    
    read_buffer_map[tag]->supply(dma_op->data, dma_op->len);
    req->acquired_len += dma_op->len;
    
    auto& lpn_req = frontReq(lpn_req_map[tag]);
    if (lpn_req != nullptr){
      std::cerr << "lpn_req is not null" << std::endl;
      // assert(0);
      lpn_req->acquired_len += dma_op->len;
    }

  }
  // handle response to DMA write request
  else {
    uint32_t tag = dma_op->tag;
    auto& req = frontReq(dram_req_map[tag]);
    req->inflight = false;    
    write_buffer_map[tag]->pop(dma_op->len);
    req->acquired_len += dma_op->len;

    auto& lpn_req = frontReq(lpn_req_map[tag]);
    if (lpn_req != nullptr){
      lpn_req->acquired_len += dma_op->len;
    }
  }

  // every time after dma completes, launch the functional simulation. 
  uint64_t insn_phy_addr = Registers_.insn_phy_addr_hh; 
  insn_phy_addr = insn_phy_addr << 32 | Registers_.insn_phy_addr_lh;
  uint32_t insn_count = NUM_INSN; //Registers_.insn_count;
  if(!VTADeviceRun(vta_func_device, insn_phy_addr, insn_count, 10000000)){
      std::cerr << "VTADeviceRun finished " << std::endl;
      Registers_.status = 0x4;
  }

  // uint64_t next_ts = TimePs();
  // std::cerr << "preparing to send req" << std::endl;
  // scans pending dram requests and issues DMA requests
  for (auto &kv : dram_req_map) {
    auto &req = frontReq(kv.second);
    if (req == nullptr) continue;
    if (req->inflight == false) {
      if(req->len == req->acquired_len) continue;
      auto bytes_to_req = std::min<uint64_t>(req->len - req->acquired_len, DMA_BLOCK_SIZE);
      if(req->rw == READ_REQ){
        auto dma_op = std::make_unique<VTADmaReadOp<DMA_BLOCK_SIZE>>(req->addr+req->acquired_len, bytes_to_req, req->id);
        IssueDma(std::move(dma_op));
      }
      else{
        auto dma_op = std::make_unique<VTADmaWriteOp>(req->addr+req->acquired_len, bytes_to_req, req->id);
        auto write_head = write_buffer_map[req->id]->getHead();
        std::memcpy(dma_op->buffer, write_head, bytes_to_req);
        IssueDma(std::move(dma_op));
      }
      req->inflight = true;
    }
  }

  // only starts lpn after the func sim is done
  // if(Registers_.status == 0x4){
    uint64_t next_ts = NextCommitTime(t_list, T_SIZE);
    
  #if VTA_DEBUG
      std::cerr << "next_ts=" << next_ts <<  " TimePs=" << TimePs() << " lpnLarge=" << lpn::LARGE << "\n";
  #endif
      assert(
          next_ts >= TimePs() &&
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
  // }
}

void VTABm::ExecuteEvent(std::unique_ptr<pciebm::TimedEvent> evt) {
  // commit all transitions who can commit at evt.time
  // alternatively, commit transitions one by one.
  // UpdateClk(TimePs());
  CommitAtTime(t_list, T_SIZE, evt->time);
  uint64_t next_ts = NextCommitTime(t_list, T_SIZE);

  if(next_ts == lpn::LARGE && Registers_.status == 0x4){
    Registers_.status = 0x2;
    TransitionCountLog(t_list, T_SIZE);
    return;
  }

#if VTA_DEBUG
  std::cerr << "lpn exec: evt time=" << evt->time << " TimePs=" << TimePs()
            << " next_ts=" << next_ts <<  " lpnLarge=" << lpn::LARGE << "\n";
#endif
  // only schedule an event if one doesn't exist yet
  assert(
      next_ts >= TimePs() &&
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
  uint32_t insn_count = NUM_INSN;//Registers_.insn_count;
  std::cerr << "insn_phy_addr: " << insn_phy_addr << " insn_count: " << insn_count << std::endl;
  if(!VTADeviceRun(vta_func_device, insn_phy_addr, insn_count, 10000000)){
      std::cerr << "VTADeviceRun finished " << std::endl;
      Registers_.status = 0x4;
  }
  for (auto &kv : dram_req_map) {
    auto &req = frontReq(kv.second);
    if (req == nullptr) continue;
    if (req->inflight == false) {
      if(req->len == req->acquired_len) continue;
      auto bytes_to_req = std::min<uint64_t>(req->len - req->acquired_len, DMA_BLOCK_SIZE);
      if(req->rw == READ_REQ){
        auto dma_op = std::make_unique<VTADmaReadOp<DMA_BLOCK_SIZE>>(req->addr+req->acquired_len, bytes_to_req, req->id);
        IssueDma(std::move(dma_op));
      }
      else{
        auto dma_op = std::make_unique<VTADmaWriteOp>(req->addr+req->acquired_len, bytes_to_req, req->id);
        auto write_head = write_buffer_map[req->id]->getHead();
        std::memcpy(dma_op->buffer, write_head, bytes_to_req);
        IssueDma(std::move(dma_op));
      }
      req->inflight = true;
    }
  }
  if( next_ts == lpn::LARGE && Registers_.status == 0x4 && !next_scheduled ){
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
