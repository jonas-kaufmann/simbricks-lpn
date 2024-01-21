#include "include/jpeg_decoder_bm.hh"
#include <bits/stdint-uintn.h>
#include <bits/types/siginfo_t.h>
#include <signal.h>
#include "lpn_def/lpn_def.hh"
#include "lpn_setup/driver.hpp"
#include "../lpn_common/lpn_sim.hh"
#include <simbricks/pciebm/pciebm.hh>

static JpegDecoderBm jpeg_decoder{};

static void sigint_handler(int dummy) {
  jpeg_decoder.SIGINTHandler();
}

static void sigusr1_handler(int dummy) {
  jpeg_decoder.SIGUSR1Handler();
}

static void sigusr2_handler(int dummy) {
  jpeg_decoder.SIGUSR2Handler();
}

static int frequency_in_mhz = 200;
static uint64_t PsToCycles(uint64_t ps){
  return ps*frequency_in_mhz/1000000;
}

void JpegDecoderBm::SetupIntro(struct SimbricksProtoPcieDevIntro &dev_intro) {
}

void JpegDecoderBm::RegRead(uint8_t bar, uint64_t addr, void *dest,
                            size_t len) {
}

void JpegDecoderBm::RegWrite(uint8_t bar, uint64_t addr, const void *src,
                             size_t len) {
}

void JpegDecoderBm::DmaComplete(pciebm::DMAOp &dma_op) {
  // TODO produce tokens for the LPN here
  uint64_t timestamp = PsToCycles(TimePs());
  UpdateLpnState(static_cast<uint8_t*>(dma_op.data), dma_op.len, timestamp);

  //IntXIssue(bool level);
  //MsiXIssue(uint8_t vec)
}

void JpegDecoderBm::ExecuteEvent(pciebm::TimedEvent &evt) {
  // TODO I'd suggest we represent the firing of transitions as events.
  // Furthermore, if an event is triggered, we check whether there's a token in
  // an output place and if so, invoke the functional code.
  
  // commit all transitions who can commit at evt.time
  // alternatively, commit transitions one by one.
  CommitAtTime(t_list, T_SIZE, evt.time);

  // find out the next earliest event time.
  int next_ts = NextCommitTime(t_list, T_SIZE);
  if(next_ts != lpn::LARGE){
    pciebm::TimedEvent* new_evt = new pciebm::TimedEvent();
    EventSchedule(*new_evt);
  }
   if(IsCurImgFinished()){
    // write out data
    pciebm::DMAOp* dma_op = new pciebm::DMAOp();
    dma_op->data = GetMOutputR();
    dma_op->len = GetSizeOfRGB();
    // TODO fill in
    dma_op->dma_addr = 0;
    dma_op->write = true;
    IssueDma(*dma_op);
    dma_op->data = GetMOutputG();
    dma_op->len = GetSizeOfRGB();
    // TODO fill in
    dma_op->dma_addr = 0;
    dma_op->write = true;
    IssueDma(*dma_op);
    dma_op->data = GetMOutputB();
    dma_op->len = GetSizeOfRGB();
    // TODO fill in
    dma_op->dma_addr = 0;
    dma_op->write = true;
    IssueDma(*dma_op);
    IntXIssue(0);
  }

  // IssueDma(DMAOp &dma_op)
 
  // else: No more event
}

void JpegDecoderBm::DevctrlUpdate(
    struct SimbricksProtoPcieH2DDevctrl &devctrl) {
}

int main(int argc, char *argv[]) {
  signal(SIGINT, sigint_handler);
  signal(SIGUSR1, sigusr1_handler);
  signal(SIGUSR1, sigusr2_handler);
  jpeg_decoder.ParseArgs(argc, argv);
  jpeg_decoder.RunMain();
  
}
