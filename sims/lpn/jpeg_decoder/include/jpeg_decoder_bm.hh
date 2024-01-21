#pragma once

#include <simbricks/pciebm/pciebm.hh>

#include "jpeg_decoder_regs.hh"

class JpegDecoderBm : public pciebm::PcieBM {
  void SetupIntro(struct SimbricksProtoPcieDevIntro &dev_intro) override;

  void RegRead(uint8_t bar, uint64_t addr, void *dest, size_t len) override;

  void RegWrite(uint8_t bar, uint64_t addr, const void *src,
                size_t len) override;

  void DmaComplete(pciebm::DMAOp &dma_op) override;

  void ExecuteEvent(pciebm::TimedEvent &evt) override;

  void DevctrlUpdate(struct SimbricksProtoPcieH2DDevctrl &devctrl) override;

 private:
  JpegDecoderRegs Registers_;
  uint64_t BytesRead_;
  uint8_t *DecodedImgData_;
};

template <uint64_t BufferLen>
struct JpegDecoderDmaReadOp : public pciebm::DMAOp {
  JpegDecoderDmaReadOp(uint64_t dma_addr, size_t len)
      : pciebm::DMAOp{false, dma_addr, len, buffer_} {
  }

 private:
  uint8_t buffer_[BufferLen];
};

struct JpegDecoderDmaWriteOp : public pciebm::DMAOp {
  JpegDecoderDmaWriteOp(uint64_t dma_addr, size_t len, uint8_t *data)
      : pciebm::DMAOp{true, dma_addr, len, data} {
  }
  bool last_block;
};