/*
 * Copyright 2024 Max Planck Institute for Software Systems, and
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

#include "lib/simbricks/pciebm/pciebm.hh"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>

#include "simbricks/base/if.h"
#include "simbricks/pcie/if.h"

extern "C" {
#include <simbricks/base/proto.h>
}

#define DEBUG_PCIEBM 0

namespace pciebm {

void PcieBM::SIGINTHandler() {
  exiting_ = true;
}

void PcieBM::SIGUSR1Handler() {
  fprintf(stderr, "main_time = %lu\n", TimePs());
}

void PcieBM::SIGUSR2Handler() {
  stat_flag_ = true;
}

volatile union SimbricksProtoPcieD2H *PcieBM::D2HAlloc() {
  if (SimbricksBaseIfInTerminated(&pcieif_.base)) {
    fprintf(stderr, "PcieBM::D2HAlloc: peer already terminated\n");
    abort();
  }

  volatile union SimbricksProtoPcieD2H *msg;
  bool first = true;
  while ((msg = SimbricksPcieIfD2HOutAlloc(&pcieif_, main_time_)) == nullptr) {
    if (first) {
      fprintf(stderr, "D2HAlloc: warning waiting for entry (%zu)\n",
              pcieif_.base.out_pos);
      first = false;
    }
    YieldPoll();
  }

  if (!first)
    fprintf(stderr, "D2HAlloc: entry successfully allocated\n");

  return msg;
}

void PcieBM::IssueDma(std::unique_ptr<DMAOp> dma_op) {
  if(dma_op->write){
    if (dma_write_pending_.size() < dma_write_max_pending_) {
  // can directly issue
  #if DEBUG_PCIEBM
      std::cout << "PcieBM::IssueDma() main_time " << main_time_/1000000
                << " issuing dma " << (dma_op->write ? "write" : "read") << " op "
                << dma_op.get() << " addr " << dma_op->dma_addr << " len "
                << dma_op->len << " pending " << dma_write_pending_.size() << std::endl;
  #endif
      DmaDo(std::move(dma_op));
    } else {
  #if DEBUG_PCIEBM
      printf(
          "main_time = %lu: pciebm: enqueuing dma op %p addr %lx len %zu pending "
          "%zu\n",
          main_time_/1000000, dma_op.get(), dma_op->dma_addr, dma_op->len,
          dma_write_pending_.size());
  #endif
      dma_write_queue_.emplace(std::move(dma_op));
    }
  }else{
        if (dma_read_pending_.size() < dma_read_max_pending_) {
  // can directly issue
  #if DEBUG_PCIEBM
      std::cout << "PcieBM::IssueDma() main_time " << main_time_/1000000
                << " issuing dma " << (dma_op->write ? "write" : "read") << " op "
                << dma_op.get() << " addr " << dma_op->dma_addr << " len "
                << dma_op->len << " pending " << dma_read_pending_.size() << std::endl;
  #endif
      DmaDo(std::move(dma_op));
    } else {
  #if DEBUG_PCIEBM
      printf(
          "main_time = %lu: pciebm: enqueuing dma op %p addr %lx len %zu pending "
          "%zu\n",
          main_time_/1000000, dma_op.get(), dma_op->dma_addr, dma_op->len,
          dma_read_pending_.size());
  #endif
      dma_read_queue_.emplace(std::move(dma_op));
    }
  }
}

void PcieBM::DmaTrigger() {
  if (!(dma_read_queue_.empty() || dma_read_pending_.size() >= dma_read_max_pending_)){
    std::unique_ptr<DMAOp> dma_op = std::move(dma_read_queue_.front());
    dma_read_queue_.pop();
    DmaDo(std::move(dma_op));
  }
  if (!(dma_write_queue_.empty() || dma_write_pending_.size() >= dma_write_max_pending_)){
    std::unique_ptr<DMAOp> dma_op2 = std::move(dma_write_queue_.front());
    dma_write_queue_.pop();
    DmaDo(std::move(dma_op2));
  }
}

void PcieBM::DmaDo(std::unique_ptr<DMAOp> dma_op) {
  if (SimbricksBaseIfInTerminated(&pcieif_.base))
    return;

#if DEBUG_PCIEBM
  printf(
      "main_time = %lu: pciebm: executing dma_op %p addr %lx len %zu pending "
      "(r%zu,w%zu)\n",
      main_time_/1000000, dma_op.get(), dma_op->dma_addr, dma_op->len,
      dma_read_pending_.size(), dma_write_pending_.size());
#endif

  volatile union SimbricksProtoPcieD2H *msg = D2HAlloc();

  size_t maxlen = SimbricksPcieIfD2HOutMsgLen(&pcieif_);
  if (dma_op->write) {
    volatile struct SimbricksProtoPcieD2HWrite *write = &msg->write;
    if (maxlen < sizeof(*write) + dma_op->len) {
      fprintf(stderr,
              "issue_dma: write too big (%zu), can only fit up "
              "to (%zu)\n",
              dma_op->len, maxlen - sizeof(*write));
      abort();
    }

    write->req_id = reinterpret_cast<uintptr_t>(dma_op.get());
    write->offset = dma_op->dma_addr;
    write->len = dma_op->len;
    memcpy(const_cast<uint8_t *>(write->data), dma_op->data, dma_op->len);

#if DEBUG_PCIEBM
    uint8_t *tmp = static_cast<uint8_t *>(dma_op->data);
    printf("main_time = %lu: pciebm: dma write data: \n", main_time_/1000000);
    for (size_t i = 0; i < dma_op->len; i++) {
      printf("%02X ", *tmp);
      tmp++;
    }
#endif
    SimbricksPcieIfD2HOutSend(&pcieif_, msg,
                              SIMBRICKS_PROTO_PCIE_D2H_MSG_WRITE);
  } else {
    volatile struct SimbricksProtoPcieD2HRead *read = &msg->read;
    if (maxlen < sizeof(struct SimbricksProtoPcieH2DReadcomp) + dma_op->len) {
      fprintf(
          stderr, "issue_dma: read too big (%zu), can only fit up to (%zu)\n",
          dma_op->len, maxlen - sizeof(struct SimbricksProtoPcieH2DReadcomp));
      abort();
    }

    read->req_id = reinterpret_cast<uintptr_t>(dma_op.get());
    read->offset = dma_op->dma_addr;
    read->len = dma_op->len;
    SimbricksPcieIfD2HOutSend(&pcieif_, msg, SIMBRICKS_PROTO_PCIE_D2H_MSG_READ);
  }

  if(dma_op->write){
    auto inserted = dma_write_pending_.emplace(
      reinterpret_cast<uintptr_t>(dma_op.get()), std::move(dma_op));
    assert(inserted.second &&
         "PcieBM::DMADo() Inserting dma_op into dma_pending_ failed.");
    // std::cout << "pcieBM::DmaDo() TimePs()=" << TimePs() << " dma_write_pending.size()=" << dma_write_pending_.size()
    //         << " dma_write_queue_.size()=" << dma_write_queue_.size() << "\n";
  }else{
    auto inserted = dma_read_pending_.emplace(
      reinterpret_cast<uintptr_t>(dma_op.get()), std::move(dma_op));
    assert(inserted.second &&
         "PcieBM::DMADo() Inserting dma_op into dma_pending_ failed.");
    // std::cout << "pcieBM::DmaDo() TimePs()=" << TimePs() << " dma_read_pending.size()=" << dma_read_pending_.size()
            // << " dma_read_queue_.size()=" << dma_read_queue_.size() << "\n";
  }
}

void PcieBM::MsiIssue(uint8_t vec) {
  if (SimbricksBaseIfInTerminated(&pcieif_.base))
    return;

  volatile union SimbricksProtoPcieD2H *msg = D2HAlloc();
#if DEBUG_PCIEBM
  printf("main_time = %lu: pciebm: issue MSI interrupt vec %u\n", main_time_/1000000,
         vec);
#endif
  volatile struct SimbricksProtoPcieD2HInterrupt *intr = &msg->interrupt;
  intr->vector = vec;
  intr->inttype = SIMBRICKS_PROTO_PCIE_INT_MSI;

  SimbricksPcieIfD2HOutSend(&pcieif_, msg,
                            SIMBRICKS_PROTO_PCIE_D2H_MSG_INTERRUPT);
}

void PcieBM::MsiXIssue(uint8_t vec) {
  if (SimbricksBaseIfInTerminated(&pcieif_.base))
    return;

  volatile union SimbricksProtoPcieD2H *msg = D2HAlloc();
#if DEBUG_PCIEBM
  printf("main_time = %lu: pciebm: issue MSI-X interrupt vec %u\n", main_time_/1000000,
         vec);
#endif
  volatile struct SimbricksProtoPcieD2HInterrupt *intr = &msg->interrupt;
  intr->vector = vec;
  intr->inttype = SIMBRICKS_PROTO_PCIE_INT_MSIX;

  SimbricksPcieIfD2HOutSend(&pcieif_, msg,
                            SIMBRICKS_PROTO_PCIE_D2H_MSG_INTERRUPT);
}

void PcieBM::IntXIssue(bool level) {
  if (SimbricksBaseIfInTerminated(&pcieif_.base))
    return;

  volatile union SimbricksProtoPcieD2H *msg = D2HAlloc();
#if DEBUG_PCIEBM
  printf("main_time = %lu: pciebm: set intx interrupt %u\n", main_time_/1000000, level);
#endif
  volatile struct SimbricksProtoPcieD2HInterrupt *intr = &msg->interrupt;
  intr->vector = 0;
  intr->inttype = (level ? SIMBRICKS_PROTO_PCIE_INT_LEGACY_HI
                         : SIMBRICKS_PROTO_PCIE_INT_LEGACY_LO);

  SimbricksPcieIfD2HOutSend(&pcieif_, msg,
                            SIMBRICKS_PROTO_PCIE_D2H_MSG_INTERRUPT);
}

void PcieBM::EventSchedule(std::unique_ptr<TimedEvent> evt) {
  events_.push(std::move(evt));
}

void PcieBM::H2DRead(volatile struct SimbricksProtoPcieH2DRead *read) {
  volatile union SimbricksProtoPcieD2H *msg;
  volatile struct SimbricksProtoPcieD2HReadcomp *readcomp;

  msg = D2HAlloc();
  readcomp = &msg->readcomp;

  // NOLINTNEXTLINE(google-readability-casting)
  RegRead(read->bar, read->offset, (void *)readcomp->data, read->len);
  readcomp->req_id = read->req_id;

#if DEBUG_PCIEBM
  uint64_t dbg_val = 0;
  // NOLINTNEXTLINE(google-readability-casting)
  memcpy(&dbg_val, (const void *)readcomp->data,
         read->len <= 8 ? read->len : 8);
  // printf("main_time = %lu: pciebm: read(off=0x%lx, len=%u, val=0x%lx)\n",
  //        main_time_/1000000, read->offset, read->len, dbg_val);
#endif

  SimbricksPcieIfD2HOutSend(&pcieif_, msg,
                            SIMBRICKS_PROTO_PCIE_D2H_MSG_READCOMP);
}

void PcieBM::H2DWrite(volatile struct SimbricksProtoPcieH2DWrite *write,
                      bool posted) {
  volatile union SimbricksProtoPcieD2H *msg;
  volatile struct SimbricksProtoPcieD2HWritecomp *writecomp;

#if DEBUG_PCIEBM
  uint64_t dbg_val = 0;
  // NOLINTNEXTLINE(google-readability-casting)
  memcpy(&dbg_val, (const void *)write->data, write->len <= 8 ? write->len : 8);
  printf(
      "main_time = %lu: pciebm: write(off=0x%lx, len=%u, val=0x%lx, "
      "posted=%u)\n",
      main_time_/1000000, write->offset, write->len, dbg_val, posted);
#endif
  // NOLINTNEXTLINE(google-readability-casting)
  RegWrite(write->bar, write->offset, (void *)write->data, write->len);

  if (!posted) {
    msg = D2HAlloc();
    writecomp = &msg->writecomp;
    writecomp->req_id = write->req_id;

    SimbricksPcieIfD2HOutSend(&pcieif_, msg,
                              SIMBRICKS_PROTO_PCIE_D2H_MSG_WRITECOMP);
  }
}

void PcieBM::H2DReadcomp(
    volatile struct SimbricksProtoPcieH2DReadcomp *readcomp) {
  uintptr_t dma_op_ptr = static_cast<uintptr_t>(readcomp->req_id);
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  std::unique_ptr<DMAOp> dma_op =
      std::move(dma_read_pending_.extract(dma_op_ptr).mapped());

#if DEBUG_PCIEBM
  printf("main_time = %lu: pciebm: completed dma read op %p addr %lx len %zu\n",
         main_time_/1000000, dma_op.get(), dma_op->dma_addr, dma_op->len);
#endif

  memcpy(dma_op->data, const_cast<uint8_t *>(readcomp->data), dma_op->len);
  DmaComplete(std::move(dma_op));
  DmaTrigger();
}

void PcieBM::H2DWritecomp(
    volatile struct SimbricksProtoPcieH2DWritecomp *writecomp) {
  uintptr_t dma_op_ptr = static_cast<uintptr_t>(writecomp->req_id);
  std::unique_ptr<DMAOp> dma_op =
      std::move(dma_write_pending_.extract(dma_op_ptr).mapped());

#if DEBUG_PCIEBM
  printf(
      "main_time = %lu: pciebm: completed dma write op %p addr %lx len %zu\n",
      main_time_/1000000, dma_op.get(), dma_op->dma_addr, dma_op->len);
#endif

  DmaComplete(std::move(dma_op));
  DmaTrigger();
}

void PcieBM::H2DDevctrl(volatile struct SimbricksProtoPcieH2DDevctrl *devctrl) {
  // NOLINTNEXTLINE(google-readability-casting)
  DevctrlUpdate(*(struct SimbricksProtoPcieH2DDevctrl *)devctrl);
}

bool PcieBM::PollH2D() {
  volatile union SimbricksProtoPcieH2D *msg =
      SimbricksPcieIfH2DInPoll(&pcieif_, main_time_);
  uint8_t type;

  h2d_poll_total_ += 1;
  if (stat_flag_) {
    s_h2d_poll_total_ += 1;
  }

  if (msg == nullptr)
    return false;

  h2d_poll_suc_ += 1;
  if (stat_flag_) {
    s_h2d_poll_suc_ += 1;
  }

  type = SimbricksPcieIfH2DInType(&pcieif_, msg);
  switch (type) {
    case SIMBRICKS_PROTO_PCIE_H2D_MSG_READ:
      H2DRead(&msg->read);
      break;

    case SIMBRICKS_PROTO_PCIE_H2D_MSG_WRITE:
      H2DWrite(&msg->write, false);
      break;

    case SIMBRICKS_PROTO_PCIE_H2D_MSG_WRITE_POSTED:
      H2DWrite(&msg->write, true);
      break;

    case SIMBRICKS_PROTO_PCIE_H2D_MSG_READCOMP:
      H2DReadcomp(&msg->readcomp);
      break;

    case SIMBRICKS_PROTO_PCIE_H2D_MSG_WRITECOMP:
      H2DWritecomp(&msg->writecomp);
      break;

    case SIMBRICKS_PROTO_PCIE_H2D_MSG_DEVCTRL:
      H2DDevctrl(&msg->devctrl);
      break;

    case SIMBRICKS_PROTO_MSG_TYPE_SYNC:
      h2d_poll_sync_ += 1;
      if (stat_flag_) {
        s_h2d_poll_sync_ += 1;
      }
      break;

    case SIMBRICKS_PROTO_MSG_TYPE_TERMINATE:
      fprintf(stderr, "poll_h2d: peer terminated\n");
      break;

    default:
      fprintf(stderr, "poll_h2d: unsupported type=%u\n", type);
  }

  SimbricksPcieIfH2DInDone(&pcieif_, msg);
  return true;
}

uint64_t PcieBM::TimePs() const {
  return main_time_;
}

std::optional<uint64_t> PcieBM::EventNext() {
  if (events_.empty())
    return std::nullopt;

  return {events_.top()->time};
}

bool PcieBM::EventTrigger() {
  if (events_.empty())
    return false;

  if (events_.top()->time > main_time_)
    return false;

  std::unique_ptr<TimedEvent> evt =
      // The const_cast here to get rid of the const qualifier from
      // `events_top()` is only fine because we pop right after. Otherwise we
      // would violate the invariants of the priority_queue.
      std::move(const_cast<std::unique_ptr<TimedEvent> &>(events_.top()));
  events_.pop();

  ExecuteEvent(std::move(evt));
  return true;
}

void PcieBM::YieldPoll() {
}

bool PcieBM::PcieIfInit() {
  struct SimbricksBaseIfSHMPool pool;
  struct SimBricksBaseIfEstablishData ests;
  struct SimbricksProtoPcieHostIntro h_intro;

  std::memset(&pool, 0, sizeof(pool));
  std::memset(&ests, 0, sizeof(ests));

  ests.base_if = &pcieif_.base;
  ests.tx_intro = &dintro_;
  ests.tx_intro_len = sizeof(dintro_);
  ests.rx_intro = &h_intro;
  ests.rx_intro_len = sizeof(h_intro);

  if (SimbricksBaseIfInit(&pcieif_.base, &pcieParams_)) {
    std::cerr << "PcieIfInit: SimbricksBaseIfInit failed\n";
    return false;
  }

  if (SimbricksBaseIfSHMPoolCreate(
          &pool, shmPath_, SimbricksBaseIfSHMSize(&pcieif_.base.params)) != 0) {
    std::cerr << "PcieIfInit: SimbricksBaseIfSHMPoolCreate failed\n";
    return false;
  }

  if (SimbricksBaseIfListen(&pcieif_.base, &pool) != 0) {
    std::cerr << "PcieIfInit: SimbricksBaseIfListen failed\n";
    return false;
  }

  if (SimBricksBaseIfEstablish(&ests, 1)) {
    std::cerr << "PciIfInit: SimBricksBaseIfEstablish failed\n";
    return false;
  }
  return true;
}

bool PcieBM::ParseArgs(int argc, char *argv[]) {
  SimbricksPcieIfDefaultParams(&pcieParams_);

  if (argc < 3 || argc > 6) {
    fprintf(stderr,
            "Usage: PcieBM PCI-SOCKET SHM [START-TICK] [SYNC-PERIOD] "
            "[PCI-LATENCY]\n");
    return false;
  }
  if (argc >= 4)
    main_time_ = strtoull(argv[3], nullptr, 0);
  if (argc >= 5)
    pcieParams_.sync_interval = strtoull(argv[4], nullptr, 0) * 1000ULL;
  if (argc >= 6)
    pcieParams_.link_latency = strtoull(argv[5], nullptr, 0) * 1000ULL;

  pcieParams_.sock_path = argv[1];
  shmPath_ = argv[2];
  return true;
}

int PcieBM::RunMain() {
  uint64_t next_ts;
  uint64_t max_step = 10000;

  memset(&dintro_, 0, sizeof(dintro_));
  SetupIntro(dintro_);

  if (!PcieIfInit()) {
    return EXIT_FAILURE;
  }
  bool sync_pci = SimbricksBaseIfSyncEnabled(&pcieif_.base);
  fprintf(stderr, "sync_pci=%d\n", sync_pci);

  while (!exiting_) {
    // send sync messages
    while (SimbricksPcieIfD2HOutSync(&pcieif_, main_time_)) {
      YieldPoll();
    }
    // process everything up to the current timestamp
    // process all available messages and wait until we actually get one with
    // a higher timestamp
    do {
      PollH2D();
    } while (SimbricksPcieIfH2DInTimestamp(&pcieif_) <= main_time_);
    // process all events that are due
    while (EventTrigger()) {
    }

    if (sync_pci) {
      next_ts = std::min(SimbricksPcieIfH2DInTimestamp(&pcieif_),
                         SimbricksPcieIfD2HOutNextSync(&pcieif_));
    } else {
      next_ts = main_time_ + max_step;
    }

    std::optional<uint64_t> ev_ts = EventNext();
    if (ev_ts) {
      next_ts = std::min(*ev_ts, next_ts);
    }
    assert(next_ts > main_time_);
    main_time_ = next_ts;
  }

  /* print statistics */
  fprintf(stderr, "exit main_time: %lu\n", main_time_);
  fprintf(stderr, "%20s: %22lu %20s: %22lu  poll_suc_rate: %Lf\n",
          "h2d_poll_total", h2d_poll_total_, "h2d_poll_suc", h2d_poll_suc_,
          static_cast<long double>(h2d_poll_suc_) / h2d_poll_total_);

  fprintf(stderr, "%65s: %22lu  sync_rate: %Lf\n", "h2d_poll_sync",
          h2d_poll_sync_,
          static_cast<long double>(h2d_poll_sync_) / h2d_poll_suc_);

  fprintf(stderr, "%20s: %22lu %20s: %22lu  poll_suc_rate: %Lf\n",
          "n2d_poll_total", n2d_poll_total_, "n2d_poll_suc", n2d_poll_suc_,
          static_cast<long double>(n2d_poll_suc_) / n2d_poll_total_);

  fprintf(stderr, "%65s: %22lu  sync_rate: %Lf\n", "n2d_poll_sync",
          n2d_poll_sync_,
          static_cast<long double>(n2d_poll_sync_) / n2d_poll_suc_);

  fprintf(stderr, "%20s: %22lu %20s: %22lu  sync_rate: %Lf\n", "recv_total",
          h2d_poll_suc_ + n2d_poll_suc_, "recv_sync",
          h2d_poll_sync_ + n2d_poll_sync_,
          static_cast<long double>(h2d_poll_sync_ + n2d_poll_sync_) /
              (h2d_poll_suc_ + n2d_poll_suc_));

  fprintf(stderr, "%20s: %22lu %20s: %22lu  poll_suc_rate: %Lf\n",
          "s_h2d_poll_total", s_h2d_poll_total_, "s_h2d_poll_suc",
          s_h2d_poll_suc_,
          static_cast<long double>(s_h2d_poll_suc_) / s_h2d_poll_total_);

  fprintf(stderr, "%65s: %22lu  sync_rate: %Lf\n", "s_h2d_poll_sync",
          s_h2d_poll_sync_,
          static_cast<long double>(s_h2d_poll_sync_) / s_h2d_poll_suc_);

  fprintf(stderr, "%20s: %22lu %20s: %22lu  poll_suc_rate: %Lf\n",
          "s_n2d_poll_total", s_n2d_poll_total_, "s_n2d_poll_suc",
          s_n2d_poll_suc_,
          static_cast<long double>(s_n2d_poll_suc_) / s_n2d_poll_total_);

  fprintf(stderr, "%65s: %22lu  sync_rate: %Lf\n", "s_n2d_poll_sync",
          s_n2d_poll_sync_,
          static_cast<long double>(s_n2d_poll_sync_) / s_n2d_poll_suc_);

  fprintf(stderr, "%20s: %22lu %20s: %22lu  sync_rate: %Lf\n", "s_recv_total",
          s_h2d_poll_suc_ + s_n2d_poll_suc_, "s_recv_sync",
          s_h2d_poll_sync_ + s_n2d_poll_sync_,
          static_cast<long double>(s_h2d_poll_sync_ + s_n2d_poll_sync_) /
              (s_h2d_poll_suc_ + s_n2d_poll_suc_));
  return 0;
}

}  // namespace pciebm
