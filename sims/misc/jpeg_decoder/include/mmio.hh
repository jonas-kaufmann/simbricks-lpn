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

#include <verilated.h>

#include <deque>
#include <functional>

#define MMIO_DEBUG 0

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
              << addr << " len=" << len << " op=" << op << std::dec << "\n";
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
              << std::dec << "\n";
#endif
    op->id = id;
    op->addr = addr;
    op->len = len;
    op->value = val;
    op->isWrite = true;
    queue.push_back(op);
  }
};