#include "include/jpeg_decoder_bm.hh"

#include <bits/stdint-uintn.h>
#include <bits/types/siginfo_t.h>
#include <signal.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

#include <simbricks/pciebm/pciebm.hh>

#include "../lpn_common/lpn_sim.hh"
#include "lpn_def/lpn_def.hh"
#include "lpn_setup/driver.hpp"
#include "sims/lpn/jpeg_decoder/include/jpeg_decoder_regs.hh"
#include "sims/lpn/lpn_common/place_transition.hh"

#define FREQ_MHZ 150000000
#define FREQ_MHZ_NORMALIZED 150
#define MASK5 0b11111
#define MASK6 0b111111

namespace {
JpegDecoderBm jpeg_decoder{};

void sigint_handler(int dummy) {
  jpeg_decoder.SIGINTHandler();
}

void sigusr1_handler(int dummy) {
  jpeg_decoder.SIGUSR1Handler();
}

void sigusr2_handler(int dummy) {
  jpeg_decoder.SIGUSR2Handler();
}

}  // namespace

void JpegDecoderBm::SetupIntro(struct SimbricksProtoPcieDevIntro &dev_intro) {
  dev_intro.pci_vendor_id = 0xdead;
  dev_intro.pci_device_id = 0xbeef;
  dev_intro.pci_class = 0x40;
  dev_intro.pci_subclass = 0x00;
  dev_intro.pci_revision = 0x00;

  // request one BAR
  static_assert(sizeof(JpegDecoderRegs) <= 4096, "Registers don't fit BAR");
  dev_intro.bars[0].len = 4096;
  dev_intro.bars[0].flags = 0;

  // setup LPN initial state
  lpn_init();
}

void JpegDecoderBm::RegRead(uint8_t bar, uint64_t addr, void *dest,
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

void JpegDecoderBm::RegWrite(uint8_t bar, uint64_t addr, const void *src,
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

  uint32_t old_ctrl = Registers_.ctrl;
  uint32_t old_is_busy = Registers_.isBusy;
  std::memcpy(reinterpret_cast<uint8_t *>(&Registers_) + addr, src, len);

  // start decoding image
  if (!old_is_busy && !(old_ctrl & CTRL_REG_START_BIT) &&
      Registers_.ctrl & CTRL_REG_START_BIT) {
    // Issue DMA for fetching the image data
    Registers_.isBusy = 1;
    BytesRead_ =
        std::min<uint64_t>(Registers_.ctrl & CTRL_REG_LEN_MASK, DMA_BLOCK_SIZE);
    uint64_t src_addr = Registers_.src;
    auto dma_op = std::make_unique<JpegDecoderDmaReadOp<DMA_BLOCK_SIZE>>(
        src_addr, BytesRead_);

    // IntXIssue(false); // deassert interrupt
    IssueDma(std::move(dma_op));
    return;
  }

  // do nothing
  Registers_.ctrl &= ~CTRL_REG_START_BIT;
  Registers_.isBusy = old_is_busy;
}

void JpegDecoderBm::DmaComplete(std::unique_ptr<pciebm::DMAOp> dma_op) {
  // handle response to DMA read request
  if (!dma_op->write) {
    // produce tokens for the LPN
    UpdateLpnState(static_cast<uint8_t *>(dma_op->data), dma_op->len, TimePs());
    // std::cout << "update lpn finishes" << std::endl;
    uint64_t next_ts = NextCommitTime(t_list, T_SIZE);

#if JPEGD_DEBUG
    std::cerr << "next_ts=" << next_ts << " TimePs=" << TimePs() << "\n";
#endif
    assert(
        next_ts >= TimePs() &&
        "JpegDecoderBm::DmaComplete: Cannot schedule event for past timestamp");
    auto next_scheduled = EventNext();
    if (next_ts != lpn::LARGE &&
        (!next_scheduled || next_scheduled.value() > next_ts)) {
#if JPEGD_DEBUG
      std::cerr << "schedule next at = " << next_ts << "\n";
#endif
      auto evt = std::make_unique<pciebm::TimedEvent>();
      evt->time = next_ts;
      EventSchedule(std::move(evt));
    }

    // issue DMA request for next block
    uint32_t total_bytes = Registers_.ctrl & CTRL_REG_LEN_MASK;
    if (BytesRead_ < total_bytes) {
      uint64_t len =
          std::min<uint64_t>(total_bytes - BytesRead_, DMA_BLOCK_SIZE);

      // reuse dma_op
      dma_op->dma_addr = Registers_.src + BytesRead_;
      dma_op->len = len;
      IssueDma(std::move(dma_op));

      BytesRead_ += len;
      return;
    }
  }
  // DMA write completed
  else {
    BytesWritten_ += dma_op->len;
    if (BytesWritten_ == GetSizeOfRGB() * 2) {
      // let host know that decoding completed
      Registers_.isBusy = 0;
      BytesWritten_ = 0;
      Reset();
    }
  }
}

void JpegDecoderBm::ExecuteEvent(std::unique_ptr<pciebm::TimedEvent> evt) {
  // commit all transitions who can commit at evt.time
  // alternatively, commit transitions one by one.

  CommitAtTime(t_list, T_SIZE, evt->time);
  uint64_t next_ts = NextCommitTime(t_list, T_SIZE);

#if JPEGD_DEBUG
  std::cerr << "lpn exec: evt time=" << evt->time << " TimePs=" << TimePs()
            << " next_ts=" << next_ts << "\n";
#endif
  // only schedule an event if one doesn't exist yet
  assert(
      next_ts >= TimePs() &&
      "JpegDecoderBm::ExecuteEvent: Cannot schedule event for past timestamp");
  auto next_scheduled = EventNext();

#if JPEGD_DEBUG
  if (next_scheduled) {
    std::cerr << "event scheduled next at = " << next_scheduled.value() << "\n";
  }
#endif

  if (next_ts != lpn::LARGE &&
      (!next_scheduled || next_scheduled.value() > next_ts)) {
#if JPEGD_DEBUG
    std::cerr << "schedule next at = " << next_ts << "\n";
#endif

    evt->time = next_ts;
    evt->priority = 0;
    EventSchedule(std::move(evt));
  }

  size_t rgb_cur_len = GetCurRGBOffset();
  if (rgb_cur_len > 0) {
    size_t rgb_consumed_len = GetConsumedRGBOffset();
    if (rgb_cur_len > rgb_consumed_len) {
      size_t pixels_to_write = rgb_cur_len - rgb_consumed_len;
      assert(pixels_to_write % DMA_BLOCK_SIZE == 0);
      auto decoded_img_data = std::make_unique<uint16_t[]>(pixels_to_write);
      uint8_t *r_out = GetMOutputR();
      uint8_t *g_out = GetMOutputG();
      uint8_t *b_out = GetMOutputB();
      for (size_t i = 0; i < pixels_to_write; ++i) {
        // convert to RGB 565
        uint16_t pixel = 0;
        pixel |= (b_out[rgb_consumed_len + i] >> 3) & MASK5;
        pixel |= ((g_out[rgb_consumed_len + i] >> 2) & MASK6) << 5;
        pixel |= ((r_out[rgb_consumed_len + i] >> 3) & MASK5) << (5 + 6);
        decoded_img_data[i] = pixel;
      }
      // split image into multiple DMAs and write back
      for (size_t i = 0; i < pixels_to_write * 2; i += DMA_BLOCK_SIZE) {
        // the `* 2` is required since we have two bytes per pixel
        uint64_t dma_addr = Registers_.dst + rgb_consumed_len * 2 + i;
        auto dma_op =
            std::make_unique<JpegDecoderDmaWriteOp>(dma_addr, DMA_BLOCK_SIZE);
        uint8_t *img_data_src =
            reinterpret_cast<uint8_t *>(decoded_img_data.get()) + i;
        std::memcpy(dma_op->buffer, img_data_src, DMA_BLOCK_SIZE);
        IssueDma(std::move(dma_op));
      }
      UpdateConsumedRGBOffset(rgb_cur_len);
    }
  }
}

void JpegDecoderBm::DevctrlUpdate(
    struct SimbricksProtoPcieH2DDevctrl &devctrl) {
  // ignore this for now
  std::cerr << "warning: ignoring SimBricks DevCtrl message with flags "
            << devctrl.flags << "\n";
}

int main(int argc, char *argv[]) {
  signal(SIGINT, sigint_handler);
  signal(SIGUSR1, sigusr1_handler);
  signal(SIGUSR2, sigusr2_handler);
  if (!jpeg_decoder.ParseArgs(argc, argv)) {
    return EXIT_FAILURE;
  }
  return jpeg_decoder.RunMain();
}
