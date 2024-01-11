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

#include "lib/simbricks/pciebm/pciebm.hh"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <ctime>
#include <iostream>
#include <vector>

#include "simbricks/pcie/if.h"

extern "C" {
#include <simbricks/base/proto.h>
}

#define DEBUG_PCIEBM 1
#define DMA_MAX_PENDING 64

namespace pciebm {

static volatile int exiting = 0;

static std::vector<PcieBM *> runners;

static uint64_t h2d_poll_total = 0;
static uint64_t h2d_poll_suc = 0;
static uint64_t h2d_poll_sync = 0;
// count from signal USR2
static uint64_t s_h2d_poll_total = 0;
static uint64_t s_h2d_poll_suc = 0;
static uint64_t s_h2d_poll_sync = 0;

static uint64_t n2d_poll_total = 0;
static uint64_t n2d_poll_suc = 0;
static uint64_t n2d_poll_sync = 0;
// count from signal USR2
static uint64_t s_n2d_poll_total = 0;
static uint64_t s_n2d_poll_suc = 0;
static uint64_t s_n2d_poll_sync = 0;
static int stat_flag = 0;

static void sigint_handler(int dummy) {
  exiting = 1;
}

static void sigusr1_handler(int dummy) {
  for (PcieBM *r : runners)
    fprintf(stderr, "[%p] main_time = %lu\n", r, r->TimePs());
}

static void sigusr2_handler(int dummy) {
  stat_flag = 1;
}

volatile union SimbricksProtoPcieD2H *PcieBM::D2HAlloc() {
  if (SimbricksBaseIfInTerminated(&pcieif_.base)) {
    fprintf(stderr, "PcieBM::D2HAlloc: peer already terminated\n");
    abort();
  }

  volatile union SimbricksProtoPcieD2H *msg;
  bool first = true;
  while ((msg = SimbricksPcieIfD2HOutAlloc(&pcieif_, main_time_)) == NULL) {
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

void PcieBM::IssueDma(DMAOp &dma_op) {
  if (dma_pending_ < DMA_MAX_PENDING) {
// can directly issue
#ifdef DEBUG_PCIEBM
    printf(
        "main_time = %lu: pciebm: issuing dma op %p addr %lx len %zu pending "
        "%zu\n",
        main_time_, &dma_op, dma_op.dma_addr, dma_op.len, dma_pending_);
#endif
    DmaDo(dma_op);
  } else {
#ifdef DEBUG_PCIEBM
    printf(
        "main_time = %lu: pciebm: enqueuing dma op %p addr %lx len %zu pending "
        "%zu\n",
        main_time_, &dma_op, dma_op.dma_addr, dma_op.len, dma_pending_);
#endif
    dma_queue_.emplace_back(dma_op);
  }
}

void PcieBM::DmaTrigger() {
  if (dma_queue_.empty() || dma_pending_ == DMA_MAX_PENDING)
    return;

  DMAOp &op = dma_queue_.front();
  DmaDo(op);
  dma_queue_.pop_front();
}

void PcieBM::DmaDo(DMAOp &dma_op) {
  if (SimbricksBaseIfInTerminated(&pcieif_.base))
    return;

  volatile union SimbricksProtoPcieD2H *msg = D2HAlloc();
  dma_pending_++;
#ifdef DEBUG_PCIEBM
  printf(
      "main_time = %lu: pciebm: executing dma dma_op %p addr %lx len %zu "
      "pending "
      "%zu\n",
      main_time_, &dma_op, dma_op.dma_addr, dma_op.len, dma_pending_);
#endif

  size_t maxlen = SimbricksBaseIfOutMsgLen(&pcieif_.base);
  if (dma_op.write) {
    volatile struct SimbricksProtoPcieD2HWrite *write = &msg->write;
    if (maxlen < sizeof(*write) + dma_op.len) {
      fprintf(stderr,
              "issue_dma: write too big (%zu), can only fit up "
              "to (%zu)\n",
              dma_op.len, maxlen - sizeof(*write));
      abort();
    }

    write->req_id = (uintptr_t)&dma_op;
    write->offset = dma_op.dma_addr;
    write->len = dma_op.len;
    memcpy((void *)write->data, (void *)dma_op.data, dma_op.len);

#ifdef DEBUG_PCIEBM
    uint8_t *tmp = (uint8_t *)dma_op.data;
    printf("main_time = %lu: pciebm: dma write data: \n", main_time_);
    for (size_t d = 0; d < dma_op.len; d++) {
      printf("%02X ", *tmp);
      tmp++;
    }
#endif
    SimbricksPcieIfD2HOutSend(&pcieif_, msg,
                              SIMBRICKS_PROTO_PCIE_D2H_MSG_WRITE);
  } else {
    volatile struct SimbricksProtoPcieD2HRead *read = &msg->read;
    if (maxlen < sizeof(struct SimbricksProtoPcieH2DReadcomp) + dma_op.len) {
      fprintf(stderr,
              "issue_dma: write too big (%zu), can only fit up "
              "to (%zu)\n",
              dma_op.len,
              maxlen - sizeof(struct SimbricksProtoPcieH2DReadcomp));
      abort();
    }

    read->req_id = (uintptr_t)&dma_op;
    read->offset = dma_op.dma_addr;
    read->len = dma_op.len;
    SimbricksPcieIfD2HOutSend(&pcieif_, msg, SIMBRICKS_PROTO_PCIE_D2H_MSG_READ);
  }
}

void PcieBM::MsiIssue(uint8_t vec) {
  if (SimbricksBaseIfInTerminated(&pcieif_.base))
    return;

  volatile union SimbricksProtoPcieD2H *msg = D2HAlloc();
#ifdef DEBUG_PCIEBM
  printf("main_time = %lu: pciebm: issue MSI interrupt vec %u\n", main_time_,
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
#ifdef DEBUG_PCIEBM
  printf("main_time = %lu: pciebm: issue MSI-X interrupt vec %u\n", main_time_,
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
#ifdef DEBUG_PCIEBM
  printf("main_time = %lu: pciebm: set intx interrupt %u\n", main_time_, level);
#endif
  volatile struct SimbricksProtoPcieD2HInterrupt *intr = &msg->interrupt;
  intr->vector = 0;
  intr->inttype = (level ? SIMBRICKS_PROTO_PCIE_INT_LEGACY_HI
                         : SIMBRICKS_PROTO_PCIE_INT_LEGACY_LO);

  SimbricksPcieIfD2HOutSend(&pcieif_, msg,
                            SIMBRICKS_PROTO_PCIE_D2H_MSG_INTERRUPT);
}

void PcieBM::EventSchedule(TimedEvent &evt) {
  events_.push(evt);
}

void PcieBM::H2DRead(volatile struct SimbricksProtoPcieH2DRead *read) {
  volatile union SimbricksProtoPcieD2H *msg;
  volatile struct SimbricksProtoPcieD2HReadcomp *rc;

  msg = D2HAlloc();
  rc = &msg->readcomp;

  RegRead(read->bar, read->offset, (void *)rc->data, read->len);
  rc->req_id = read->req_id;

#ifdef DEBUG_PCIEBM
  uint64_t dbg_val = 0;
  memcpy(&dbg_val, (const void *)rc->data, read->len <= 8 ? read->len : 8);
  printf("main_time = %lu: pciebm: read(off=0x%lx, len=%u, val=0x%lx)\n",
         main_time_, read->offset, read->len, dbg_val);
#endif

  SimbricksPcieIfD2HOutSend(&pcieif_, msg,
                            SIMBRICKS_PROTO_PCIE_D2H_MSG_READCOMP);
}

void PcieBM::H2DWrite(volatile struct SimbricksProtoPcieH2DWrite *write,
                      bool posted) {
  volatile union SimbricksProtoPcieD2H *msg;
  volatile struct SimbricksProtoPcieD2HWritecomp *wc;

#ifdef DEBUG_PCIEBM
  uint64_t dbg_val = 0;
  memcpy(&dbg_val, (const void *)write->data, write->len <= 8 ? write->len : 8);
  printf(
      "main_time = %lu: pciebm: write(off=0x%lx, len=%u, val=0x%lx, "
      "posted=%u)\n",
      main_time_, write->offset, write->len, dbg_val, posted);
#endif
  RegWrite(write->bar, write->offset, (void *)write->data, write->len);

  if (!posted) {
    msg = D2HAlloc();
    wc = &msg->writecomp;
    wc->req_id = write->req_id;

    SimbricksPcieIfD2HOutSend(&pcieif_, msg,
                              SIMBRICKS_PROTO_PCIE_D2H_MSG_WRITECOMP);
  }
}

void PcieBM::H2DReadcomp(
    volatile struct SimbricksProtoPcieH2DReadcomp *readcomp) {
  DMAOp *op = (DMAOp *)(uintptr_t)readcomp->req_id;

#ifdef DEBUG_PCIEBM
  printf("main_time = %lu: pciebm: completed dma read op %p addr %lx len %zu\n",
         main_time_, op, op->dma_addr, op->len);
#endif

  memcpy(op->data, (void *)readcomp->data, op->len);
  DmaComplete(*op);

  dma_pending_--;
  DmaTrigger();
}

void PcieBM::H2DWritecomp(volatile struct SimbricksProtoPcieH2DWritecomp *wc) {
  DMAOp *op = (DMAOp *)(uintptr_t)wc->req_id;

#ifdef DEBUG_PCIEBM
  printf(
      "main_time = %lu: pciebm: completed dma write op %p addr %lx len %zu\n",
      main_time_, op, op->dma_addr, op->len);
#endif

  DmaComplete(*op);

  dma_pending_--;
  DmaTrigger();
}

void PcieBM::H2DDevctrl(volatile struct SimbricksProtoPcieH2DDevctrl *devctrl) {
  DevctrlUpdate(*(struct SimbricksProtoPcieH2DDevctrl *)devctrl);
}

void PcieBM::PollH2D() {
  volatile union SimbricksProtoPcieH2D *msg =
      SimbricksPcieIfH2DInPoll(&pcieif_, main_time_);
  uint8_t type;

  h2d_poll_total += 1;
  if (stat_flag) {
    s_h2d_poll_total += 1;
  }

  if (msg == NULL)
    return;

  h2d_poll_suc += 1;
  if (stat_flag) {
    s_h2d_poll_suc += 1;
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
#ifdef STAT_NICBM
      h2d_poll_sync += 1;
      if (stat_flag) {
        s_h2d_poll_sync += 1;
      }
#endif
      break;

    case SIMBRICKS_PROTO_MSG_TYPE_TERMINATE:
      fprintf(stderr, "poll_h2d: peer terminated\n");
      break;

    default:
      fprintf(stderr, "poll_h2d: unsupported type=%u\n", type);
  }

  SimbricksPcieIfH2DInDone(&pcieif_, msg);
}

uint64_t PcieBM::TimePs() const {
  return main_time_;
}

std::optional<uint64_t> PcieBM::EventNext() {
  if (events_.empty())
    return {};

  return {events_.top().get().time};
}

void PcieBM::EventTrigger() {
  if (events_.empty())
    return;

  TimedEvent &evt = events_.top();
  if (evt.time > main_time_)
    return;

  ExecuteEvent(evt);
  events_.pop();
}

void PcieBM::YieldPoll() {
  /* do nothing */
}

PcieBM::PcieBM(Device &dev) : main_time_(0), dev_(dev), events_(EventCmp()) {
  // mac_addr = lrand48() & ~(3ULL << 46);
  runners.push_back(this);
  dma_pending_ = 0;
  dev_.runner_ = this;

  int rfd;
  if ((rfd = open("/dev/urandom", O_RDONLY)) < 0) {
    perror("PcieBM::PcieBM: opening urandom failed");
    abort();
  }
  if (read(rfd, &mac_addr_, 6) != 6) {
    perror("PcieBM::PcieBM: reading urandom failed");
  }
  close(rfd);
  mac_addr_ &= ~3ULL;

  SimbricksNetIfDefaultParams(&netParams_);
  SimbricksPcieIfDefaultParams(&pcieParams_);
}

int PcieBM::ParseArgs(int argc, char *argv[]) {
  if (argc < 4 || argc > 10) {
    fprintf(stderr,
            "Usage: corundum_bm PCI-SOCKET ETH-SOCKET "
            "SHM [SYNC-MODE] [START-TICK] [SYNC-PERIOD] [PCI-LATENCY] "
            "[ETH-LATENCY] [MAC-ADDR]\n");
    return -1;
  }
  if (argc >= 6)
    main_time_ = strtoull(argv[5], NULL, 0);
  if (argc >= 7)
    netParams_.sync_interval = pcieParams_.sync_interval =
        strtoull(argv[6], NULL, 0) * 1000ULL;
  if (argc >= 8)
    pcieParams_.link_latency = strtoull(argv[7], NULL, 0) * 1000ULL;
  if (argc >= 9)
    netParams_.link_latency = strtoull(argv[8], NULL, 0) * 1000ULL;
  if (argc >= 10)
    mac_addr_ = strtoull(argv[9], NULL, 16);

  pcieParams_.sock_path = argv[1];
  netParams_.sock_path = argv[2];
  shmPath_ = argv[3];
  return 0;
}

int PcieBM::RunMain() {
  uint64_t next_ts;
  uint64_t max_step = 10000;

  signal(SIGINT, sigint_handler);
  signal(SIGUSR1, sigusr1_handler);
  signal(SIGUSR2, sigusr2_handler);

  memset(&dintro_, 0, sizeof(dintro_));
  SetupIntro(dintro_);

  bool sync_pci = SimbricksBaseIfSyncEnabled(&pcieif_.base);

  fprintf(stderr, "sync_pci=%d\n", sync_pci);

  while (!exiting) {
    while (SimbricksNicIfSync(&nicif_, main_time_)) {
      fprintf(stderr, "warn: SimbricksNicIfSync failed (t=%lu)\n", main_time_);
      YieldPoll();
    }

    bool first = true;
    do {
      if (!first)
        YieldPoll();
      first = false;

      PollH2D();
      EventTrigger();

      if (sync_pci) {
        next_ts = SimbricksNicIfNextTimestamp(&nicif_);
        if (next_ts > main_time_ + max_step)
          next_ts = main_time_ + max_step;
      } else {
        next_ts = main_time_ + max_step;
      }

      uint64_t ev_ts;
      if (EventNext(ev_ts) && ev_ts < next_ts)
        next_ts = ev_ts;
    } while (next_ts <= main_time_ && !exiting);
    main_time_ = next_ts;

    YieldPoll();
  }

  fprintf(stderr, "exit main_time: %lu\n", main_time_);
#ifdef STAT_NICBM
  fprintf(stderr, "%20s: %22lu %20s: %22lu  poll_suc_rate: %f\n",
          "h2d_poll_total", h2d_poll_total, "h2d_poll_suc", h2d_poll_suc,
          (double)h2d_poll_suc / h2d_poll_total);

  fprintf(stderr, "%65s: %22lu  sync_rate: %f\n", "h2d_poll_sync",
          h2d_poll_sync, (double)h2d_poll_sync / h2d_poll_suc);

  fprintf(stderr, "%20s: %22lu %20s: %22lu  poll_suc_rate: %f\n",
          "n2d_poll_total", n2d_poll_total, "n2d_poll_suc", n2d_poll_suc,
          (double)n2d_poll_suc / n2d_poll_total);

  fprintf(stderr, "%65s: %22lu  sync_rate: %f\n", "n2d_poll_sync",
          n2d_poll_sync, (double)n2d_poll_sync / n2d_poll_suc);

  fprintf(
      stderr, "%20s: %22lu %20s: %22lu  sync_rate: %f\n", "recv_total",
      h2d_poll_suc + n2d_poll_suc, "recv_sync", h2d_poll_sync + n2d_poll_sync,
      (double)(h2d_poll_sync + n2d_poll_sync) / (h2d_poll_suc + n2d_poll_suc));

  fprintf(stderr, "%20s: %22lu %20s: %22lu  poll_suc_rate: %f\n",
          "s_h2d_poll_total", s_h2d_poll_total, "s_h2d_poll_suc",
          s_h2d_poll_suc, (double)s_h2d_poll_suc / s_h2d_poll_total);

  fprintf(stderr, "%65s: %22lu  sync_rate: %f\n", "s_h2d_poll_sync",
          s_h2d_poll_sync, (double)s_h2d_poll_sync / s_h2d_poll_suc);

  fprintf(stderr, "%20s: %22lu %20s: %22lu  poll_suc_rate: %f\n",
          "s_n2d_poll_total", s_n2d_poll_total, "s_n2d_poll_suc",
          s_n2d_poll_suc, (double)s_n2d_poll_suc / s_n2d_poll_total);

  fprintf(stderr, "%65s: %22lu  sync_rate: %f\n", "s_n2d_poll_sync",
          s_n2d_poll_sync, (double)s_n2d_poll_sync / s_n2d_poll_suc);

  fprintf(stderr, "%20s: %22lu %20s: %22lu  sync_rate: %f\n", "s_recv_total",
          s_h2d_poll_suc + s_n2d_poll_suc, "s_recv_sync",
          s_h2d_poll_sync + s_n2d_poll_sync,
          (double)(s_h2d_poll_sync + s_n2d_poll_sync) /
              (s_h2d_poll_suc + s_n2d_poll_suc));
#endif

  SimbricksNicIfCleanup(&nicif_);
  return 0;
}

void PcieBM::Device::Timed(TimedEvent &te) {
}

void PcieBM::Device::DevctrlUpdate(
    struct SimbricksProtoPcieH2DDevctrl &devctrl) {
  int_intx_en_ = devctrl.flags & SIMBRICKS_PROTO_PCIE_CTRL_INTX_EN;
  int_msi_en_ = devctrl.flags & SIMBRICKS_PROTO_PCIE_CTRL_MSI_EN;
  int_msix_en_ = devctrl.flags & SIMBRICKS_PROTO_PCIE_CTRL_MSIX_EN;
}

}  // namespace pciebm
