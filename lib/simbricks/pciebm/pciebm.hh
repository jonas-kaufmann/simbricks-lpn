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

#ifndef SIMBRICKS_PCIEBM_PCIEBM_H_
#define SIMBRICKS_PCIEBM_PCIEBM_H_

#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <vector>

#include <simbricks/base/cxxatomicfix.h>

#include "simbricks/pcie/if.h"

namespace pciebm {

struct DMAOp {
  bool write;
  uint64_t dma_addr;
  size_t len;
  void *data;
};

struct TimedEvent {
  uint64_t time;
  int priority;

  bool operator>(const TimedEvent &other) const {
    return time > other.time ||
           (time == other.time && priority > other.priority);
  }
};

/* This is an abstract base for PCIe device simulators that implement a
 * behavioral model. The idea is to inherit from this class and implement the
 * virtual methods, which are mostly callbacks for important PCIe events.
 * Furthermore, the remaining protected methods expose an API to invoke PCIe
 * requests. The private methods take care of implementing the low-level
 * SimBricks related details. */
class PcieBM {
 protected:
  /**
   * These are callback functions that need to be implemented by the behavioral
   * model
   */

  /* Initialize device specific parameters (pci dev/vendor id, BARs etc. in
   * `di`.*/
  virtual void SetupIntro(struct SimbricksProtoPcieDevIntro &devintro) = 0;

  /* Invoked when receiving a register read request `bar`:`addr` of length
   * `len`. Store the result in `dest`. */
  virtual void RegRead(uint8_t bar, uint64_t addr, void *dest, size_t len) = 0;

  /* Invoked when receiving a register read request `bar`:`addr` of length
   * `len`, with the data in `src`. */
  virtual void RegWrite(uint8_t bar, uint64_t addr, const void *src,
                        size_t len) = 0;

  /* The previously issued DMA operation `op` has been completed. */
  virtual void DmaComplete(DMAOp &dma_op) = 0;

  /* Execute the previously scheduled event `evt`. A call to this function This
  function should also free its associated memory. */
  virtual void ExecuteEvent(TimedEvent &evt) = 0;

  /* Callback for a device control update request. */
  virtual void DevctrlUpdate(struct SimbricksProtoPcieH2DDevctrl &devctrl) = 0;

  /**
   * The following functions form the API exposed to the behavioral model for
   * invoking PCIe requests and scheduling events.
   */

  /* Issue a DMA PCIe request with the details in `dma_op`. */
  void IssueDma(DMAOp &dma_op);

  /* Issue an MSI interrupt over PCIe. */
  void MsiIssue(uint8_t vec);

  /* Issue an MSI-X interrupt over PCIe. */
  void MsiXIssue(uint8_t vec);

  /* Issue a legacy interrupt over PCIe. */
  void IntXIssue(bool level);

  /* Returns the current timestamp in picoseconds. */
  uint64_t TimePs() const;

  /* Schedule an event to be executed in the future. The caller takes care of
  the memory management of `evt`. The passed reference needs to remain valid
  until `ExecuteEvent()` is invoked.
  */
  void EventSchedule(TimedEvent &evt);

  /* Returns the timestamp of the earliest event that's scheduled to be
   * executed. If no scheduled event exists, returns an empty std::optional */
  std::optional<uint64_t> EventNext();

 private:
  uint64_t main_time_;
  std::priority_queue<std::reference_wrapper<TimedEvent>,
                      std::vector<std::reference_wrapper<TimedEvent>>,
                      std::greater<>>
      events_;
  std::deque<std::reference_wrapper<DMAOp>> dma_queue_;
  size_t dma_pending_;

  struct SimbricksBaseIfParams pcieParams_;
  const char *shmPath_;
  struct SimbricksPcieIf pcieif_;
  struct SimbricksProtoPcieDevIntro dintro_;

  /* for signal handlers */
  volatile bool exiting_;
  volatile bool stat_flag_;

  uint64_t h2d_poll_total_;
  uint64_t h2d_poll_suc_;
  uint64_t h2d_poll_sync_;
  /* counted from signal USR2 */
  uint64_t s_h2d_poll_total_;
  uint64_t s_h2d_poll_suc_;
  uint64_t s_h2d_poll_sync_;

  uint64_t n2d_poll_total_;
  uint64_t n2d_poll_suc_;
  uint64_t n2d_poll_sync_;
  /* counted from signal USR2 */
  uint64_t s_n2d_poll_total_;
  uint64_t s_n2d_poll_suc_;
  uint64_t s_n2d_poll_sync_;

  volatile union SimbricksProtoPcieD2H *D2HAlloc();

  void H2DRead(volatile struct SimbricksProtoPcieH2DRead *read);
  void H2DWrite(volatile struct SimbricksProtoPcieH2DWrite *write, bool posted);
  void H2DReadcomp(volatile struct SimbricksProtoPcieH2DReadcomp *readcomp);
  void H2DWritecomp(volatile struct SimbricksProtoPcieH2DWritecomp *writecomp);
  void H2DDevctrl(volatile struct SimbricksProtoPcieH2DDevctrl *devctrl);
  void PollH2D();

  void EventTrigger();

  void DmaDo(DMAOp &dma_op);
  void DmaTrigger();

  void YieldPoll();
  int PcieIfInit();

 public:
  /** Parse command line arguments. */
  int ParseArgs(int argc, char *argv[]);

  /** Run the simulation */
  int RunMain();

  /* This handler should be invoked when receiving a SIGINT signal. */
  void SIGINTHandler();
  /* This handler should be invoked when receiving a SIGUSR1 signal. */
  void SIGUSR1Handler();
  /* This handler should be invoked when receiving a SIGUSR2 signal. */
  void SIGUSR2Handler();
};

}  // namespace pciebm
#endif  // SIMBRICKS_PCIEBM_PCIEBM_H_
