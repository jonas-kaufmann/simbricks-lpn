#pragma once

#include <simbricks/pciebm/pciebm.hh>

class JpegDecoderBm : public pciebm::PcieBM {
  void SetupIntro(struct SimbricksProtoPcieDevIntro &dev_intro) override;

  void RegRead(uint8_t bar, uint64_t addr, void *dest, size_t len) override;

  void RegWrite(uint8_t bar, uint64_t addr, const void *src,
                size_t len) override;

  void DmaComplete(pciebm::DMAOp &dma_op) override;

  void ExecuteEvent(pciebm::TimedEvent &evt) override;

  void DevctrlUpdate(struct SimbricksProtoPcieH2DDevctrl &devctrl) override;
};
