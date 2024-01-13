#include "jpeg_decoder_bm.hh"

#include <bits/types/siginfo_t.h>
#include <signal.h>

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
}

void JpegDecoderBm::ExecuteEvent(pciebm::TimedEvent &evt) {
  // TODO I'd suggest we represent the firing of transitions as events.
  // Furthermore, if an event is triggered, we check whether there's a token in
  // an output place and if so, invoke the functional code.
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