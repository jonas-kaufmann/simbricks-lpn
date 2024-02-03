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

#include <verilated.h>

#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <type_traits>

#define MMIO_DEBUG 0
#define AXI_DEBUG 0

struct AXIChannelReadAddr {
  // Signal widths
  size_t addr_bits;
  size_t id_bits;
  size_t user_bits;

  // Signals
  CData *ready;  // <-
  CData *valid;  // ->

  void *addr;     // ->
  void *id;       // ->
  void *user;     // ->
  CData *len;     // ->
  CData *size;    // ->
  CData *burst;   // ->
  CData *lock;    // ->
  CData *cache;   // ->
  CData *prot;    // ->
  CData *qos;     // ->
  CData *region;  // ->
};

struct AXIChannelReadData {
  // Signal widths
  size_t data_bits;
  size_t id_bits;
  size_t user_bits;

  // Signals
  CData *ready;  // ->
  CData *valid;  // <-

  CData *resp;  // <-
  void *data;   // <-
  void *id;     // <-
  CData *last;  // <-
};

struct AXIChannelWriteAddr {
  // Signal widths
  size_t addr_bits;
  size_t id_bits;
  size_t user_bits;

  // Signals
  CData *ready;  // <-
  CData *valid;  // ->

  void *addr;     // ->
  void *id;       // ->
  void *user;     // ->
  CData *len;     // ->
  CData *size;    // ->
  CData *burst;   // ->
  CData *lock;    // ->
  CData *cache;   // ->
  CData *prot;    // ->
  CData *qos;     // ->
  CData *region;  // ->
};

struct AXIChannelWriteData {
  // Signal widths
  size_t data_bits;
  size_t id_bits;
  size_t user_bits;

  // Signals
  CData *ready;  // <-
  CData *valid;  // ->

  void *data;   // ->
  void *strb;   // ->
  CData *last;  // ->
};

struct AXIChannelWriteResp {
  // Signal widths
  size_t id_bits;
  size_t user_bits;

  // Signals
  CData *ready;  // ->
  CData *valid;  // <-

  CData *resp;  // ->
  void *id;     // ->
  void *user;   // ->
};

class AXIReader;
class AXIWriter;

template <bool IsWrite>
class AXIOperation {
 public:
  uint64_t addr;
  size_t len;
  uint64_t id;
  uint8_t *buf;
  size_t off;
  size_t step_size;

  using issuedByT = std::conditional_t<IsWrite, AXIWriter, AXIReader> *;
  issuedByT issued_by;

  AXIOperation(uint64_t addr_, size_t len_, uint64_t id_, size_t step_size,
               issuedByT issuedBy)
      : addr(addr_),
        len(len_),
        id(id_),
        buf(new uint8_t[len_]),
        off(0),
        step_size(step_size),
        issued_by(issuedBy) {
  }

  ~AXIOperation() {
    delete[] buf;
  }
};

class AXIReader {
 public:
  using AXIOperationT = AXIOperation<false>;

 protected:
  uint64_t main_time;
  AXIChannelReadAddr addrP;
  AXIChannelReadData dataP;

  std::deque<AXIOperationT *> pending;
  AXIOperationT *curOp;
  size_t curOff;

  virtual void doRead(AXIOperationT *op) = 0;

 public:
  void readDone(AXIOperationT *op);
  void step(uint64_t ts);
};

class AXIWriter {
 public:
  using AXIOperationT = AXIOperation<true>;

 protected:
  uint64_t main_time;
  uint64_t suspend_until;
  AXIChannelWriteAddr addrP;
  AXIChannelWriteData dataP;
  AXIChannelWriteResp respP;

  std::deque<AXIOperationT *> pending;
  std::deque<AXIOperationT *> completed;
  AXIOperationT *complOp;
  bool complWasReady;

  virtual void doWrite(AXIOperationT *op) = 0;

 public:
  void writeDone(AXIOperationT *op);
  void step(uint64_t ts);
};

struct MMIOOp {
  uint64_t id;
  uint64_t addr;
  uint32_t value;
  size_t len;
  bool isWrite;
};

struct MMIOPorts {
  size_t addrBits;
  size_t dataBits;

  void *awaddr;
  // CData &awprot;
  CData &awvalid;
  CData &awready;

  void *wdata;
  void *wstrb;
  CData &wvalid;
  CData &wready;

  CData &bresp;
  CData &bvalid;
  CData &bready;

  void *araddr;
  // CData &arprot;
  CData &arvalid;
  CData &arready;

  void *rdata;
  CData &rresp;
  CData &rvalid;
  CData &rready;
};

class MMIOInterface {
 protected:
  std::deque<MMIOOp *> queue;
  MMIOOp *rCur, *wCur;

  bool rAAck, rDAck;
  bool wAAck, wDAck, wBAck;

  uint64_t main_time;

  using mmioDoneT = std::function<void(MMIOOp *, uint64_t)>;
  mmioDoneT mmio_done;

  MMIOPorts ports;

 public:
  MMIOInterface(mmioDoneT mmio_done_callback_, MMIOPorts ports_)
      : rCur(nullptr),
        wCur(nullptr),
        mmio_done(std::move(mmio_done_callback_)),
        ports(ports_) {
  }

  void step(uint64_t update_ts);

  void issueRead(uint64_t id, uint64_t addr, size_t len) {
    MMIOOp *op = new MMIOOp;
#if MMIO_DEBUG
    std::cout << main_time << " MMIO: read id=" << id << " addr=" << std::hex
              << addr << " len=" << len << " op=" << op << std::dec
              << std::endl;
#endif
    op->id = id;
    op->addr = addr;
    op->len = len;
    op->isWrite = false;
    queue.push_back(op);
  }

  void issueWrite(uint64_t id, uint64_t addr, size_t len, uint64_t val) {
    MMIOOp *op = new MMIOOp;
#if MMIO_DEBUG
    std::cout << main_time << " MMIO: write id=" << id << " addr=" << std::hex
              << addr << " len=" << len << " val=" << val << " op=" << op
              << std::dec << std::endl;
#endif
    op->id = id;
    op->addr = addr;
    op->len = len;
    op->value = val;
    op->isWrite = true;
    queue.push_back(op);
  }
};