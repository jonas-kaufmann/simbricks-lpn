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

#include "include/axi.hh"

#include <math.h>

#include <iostream>
#include <unordered_map>

void AXIReader::step(uint64_t cur_ts) {
  main_time = cur_ts;
  *addrP.ready = 1;
  if (*addrP.valid) {
    uint64_t axi_id = 0;
    memcpy(&axi_id, addrP.id, (addrP.id_bits + 7) / 8);

    uint64_t addr = 0;
    memcpy(&addr, addrP.addr, (addrP.addr_bits + 7) / 8);

    uint64_t size = pow(2, *addrP.size);
    assert(*addrP.burst == 1 && "we currently only support INCR bursts");
    AXIOperationT *axi_op =
        new AXIOperationT(addr, size * (*addrP.len + 1), axi_id, size, this);
#if AXI_DEBUG
    std::cout << main_time << " AXI R: new op=" << axi_op
              << " addr=" << axi_op->addr << " len=" << axi_op->len
              << " id=" << axi_op->id << std::endl;
#endif
    doRead(axi_op);
  }

  if (!curOp && !pending.empty()) {
    curOp = pending.front();
    curOff = 0;
    pending.pop_front();
#if AXI_DEBUG
    std::cout << main_time << " AXI R: starting response op=" << curOp
              << " ready=" << (unsigned)*dataP.ready << " id=" << curOp->id
              << std::endl;
#endif

    uint64_t data_bytes = (dataP.data_bits + 7) / 8;
    size_t align = curOp->addr % data_bytes;
    memcpy(static_cast<uint8_t *>(dataP.data) + align, curOp->buf,
           std::min(data_bytes - align, curOp->step_size));
    memcpy(dataP.id, &curOp->id, (dataP.id_bits + 7) / 8);
    *dataP.valid = 1;

    curOff += curOp->step_size;
    *dataP.last = (curOff == curOp->len);

    if (*dataP.last) {
#if AXI_DEBUG
      std::cout << main_time << " AXI R: completed op=" << curOp
                << " id=" << curOp->id << std::endl;
#endif
      delete curOp;
      curOp = nullptr;
    }
  } else if (curOp && *dataP.ready) {
#if AXI_DEBUG
    std::cout << main_time << " AXI R: step op=" << curOp << " off=" << curOff
              << " id=" << curOp->id << std::endl;
#endif
    uint64_t data_bytes = (dataP.data_bits + 7) / 8;
    size_t align = (curOp->addr + curOff) % data_bytes;
    memcpy(dataP.data, curOp->buf + curOff,
           std::min(data_bytes - align, curOp->step_size));

    curOff += curOp->step_size;
    *dataP.last = (curOff == curOp->len);
    if (*dataP.last) {
#if AXI_DEBUG
      std::cout << main_time << " AXI R: completed op=" << curOp
                << " id=" << curOp->id << std::endl;
#endif
      delete curOp;
      curOp = nullptr;
    }
  } else if (!curOp) {
    *dataP.valid = 0;
  }
}

void AXIReader::readDone(AXIOperationT *axi_op) {
#if AXI_DEBUG
  std::cout << main_time << " AXI R: enqueue op=" << axi_op << std::endl;
  std::cout << "    ";
  for (size_t i = 0; i < axi_op->len; i++) {
    std::cout << (unsigned)axi_op->buf[i] << " ";
  }
  std::cout << std::endl;
#endif
  pending.push_back(axi_op);
}

void AXIWriter::step(uint64_t cur_ts) {
  main_time = cur_ts;

  // if (main_time < suspend_until) {
  //   *respP.valid = 0;
  //   *dataP.ready = 0;
  //   return;
  // }

  if (complOp && (*respP.ready || complWasReady)) {
#if AXI_DEBUG
    std::cout << main_time << " AXI W: complete op=" << complOp << std::endl;
#endif
    delete complOp;
    complOp = nullptr;
    *respP.valid = 0;
  }

  if (!complOp && !completed.empty()) {
    complOp = completed.front();
    completed.pop_front();

#if AXI_DEBUG
    std::cout << main_time << " AXI W: issuing completion op=" << complOp
              << std::endl;
#endif

    memcpy(respP.id, &complOp->id, (respP.id_bits + 7) / 8);
    *respP.valid = 1;
    complWasReady = *respP.ready;
  }

  *addrP.ready = 1;
  if (*addrP.valid) {
    uint64_t axi_id = 0;
    memcpy(&axi_id, addrP.id, (addrP.id_bits + 7) / 8);

    uint64_t addr = 0;
    memcpy(&addr, addrP.addr, (addrP.addr_bits + 7) / 8);

    uint64_t size = pow(2, *addrP.size);
    assert(*addrP.burst == 1 && "we currently only support INCR bursts");
    AXIOperationT *axi_op =
        new AXIOperationT(addr, size * (*addrP.len + 1), axi_id, size, this);
#if AXI_DEBUG
    std::cout << main_time << " AXI W: new op=" << axi_op << " addr=" << axi_op->addr
              << " len=" << axi_op->len << " id=" << axi_op->id << std::endl;
#endif
    if (std::find_if(pending.begin(), pending.end(),
                     [axi_id](AXIOperationT *axi_op) {
                       return axi_op->id == axi_id;
                     }) != pending.end()) {
      std::cerr << "AXI W id " << axi_id << " is already pending" << std::endl;
      abort();
    }
    pending.emplace_back(axi_op);
  }

  *dataP.ready = 1;
  if (*dataP.valid) {
    if (pending.empty()) {
      std::cerr << "AXI W pending shouldn't be empty" << std::endl;
      abort();
    }

    AXIOperationT *axi_op = pending.front();

#if AXI_DEBUG
    std::cout << main_time << " AXI W: data id=" << axi_op->id << " op=" << axi_op
              << " last=" << (unsigned)*dataP.last << std::endl;
#endif

    uint64_t data_bytes = (dataP.data_bits + 7) / 8;
    size_t align = (axi_op->addr + axi_op->off) % data_bytes;
#if AXI_DEBUG
    std::cout << "AXI W: align=" << align << " off=" << axi_op->off
              << " step_size=" << axi_op->step_size << std::endl;
#endif
    memcpy(axi_op->buf + axi_op->off,
           static_cast<uint8_t *>(dataP.data) + align,
           std::min(data_bytes - align, axi_op->step_size));
    axi_op->off += axi_op->step_size;
    if (axi_op->off > axi_op->len) {
      std::cerr << "AXI W operation too long?" << std::endl;
      abort();
    } else if (axi_op->off == axi_op->len) {
      if (!*dataP.last) {
        std::cerr << "AXI W operation is done but last is not set?"
                  << std::endl;
        abort();
      }

      pending.pop_front();
      doWrite(axi_op);
      // suspend_until = main_time + 16 * (1'000'000 / 150ULL);
    }
  }
}

void AXIWriter::writeDone(AXIOperationT *axi_op) {
#if AXI_DEBUG
  std::cout << main_time << " AXI W: completed write for op=" << axi_op
            << std::endl;
#endif
  completed.push_back(axi_op);
}

void MMIOInterface::step(uint64_t update_ts) {
  main_time = update_ts;

  if (rCur) {
    /* work on active read operation */
    if (rDAck) {
      /* read fully completed */
#if MMIO_DEBUG
      std::cout << main_time << " MMIO: completed AXI read op=" << rCur
                << std::endl;
#endif
      ports.rready = 0;
      mmio_done(rCur, main_time);
      rCur = nullptr;
    } else if (ports.rvalid) {
      assert(rAAck);
      uint64_t port_width = (ports.dataBits + 7) / 8;
      uint64_t data_offset =
          rCur->addr % port_width;  // for unaligned transfers, data is
                                    // received at this offset on data port
      memcpy(&rCur->value, static_cast<uint8_t *>(ports.rdata) + data_offset,
             std::min(port_width - data_offset, sizeof(rCur->value)));
      rDAck = true;  // need to delay with ready high for a full cycle for
                     // chisel code to fully register
    }

    if (ports.arvalid && (ports.arready || rAAck)) {
      /* read addr handshake is complete */
#if MMIO_DEBUG
      std::cout << main_time
                << " MMIO: AXI read addr handshake done op=" << rCur
                << std::endl;
#endif
      ports.arvalid = 0;
      rAAck = true;
    }
  } else if (wCur) {
    /* work on active write operation */

    if (wBAck) {
      /* write fully completed */
#if MMIO_DEBUG
      std::cout << main_time << " MMIO: completed AXI wriste op=" << wCur
                << std::endl;
      // report_outputs(&top);
#endif
      ports.bready = 0;
      mmio_done(wCur, main_time);
      wCur = nullptr;
    } else if (ports.bvalid) {
      assert(wAAck && wDAck);
      wBAck = true;  // need to delay with ready high for a full cycle for
                     // chisel code to fully register
    }

    if (ports.wvalid && (ports.wready || wDAck)) {
      /* write data handshake is complete */
#if MMIO_DEBUG
      std::cout << main_time
                << " MMIO: AXI write data handshake done op=" << wCur
                << std::endl;
#endif
      ports.wvalid = 0;
      wDAck = true;
    }

    if (ports.awvalid && (ports.awready || wAAck)) {
      /* write addr handshake is complete */
#if MMIO_DEBUG
      std::cout << main_time
                << " MMIO: AXI write addr handshake done op=" << wCur
                << std::endl;
#endif
      ports.awvalid = 0;
      wAAck = true;

      wDAck = ports.wready;
      ports.wvalid = 1;
    }

  } else if (/* !top.clk && */ !queue.empty()) {
    /* issue new operation */
    MMIOOp *axi_op = queue.front();
#if MMIO_DEBUG
    std::cout << main_time << " MMIO: issuing new op on axi op=" << op
              << std::endl;
#endif
    queue.pop_front();
    if (!axi_op->isWrite) {
      /* issue new read */
      rCur = axi_op;

      memcpy(ports.araddr, &rCur->addr, (ports.addrBits + 7) / 8);
      rAAck = ports.arready;
      ports.arvalid = 1;
      rDAck = false;
      ports.rready = 1;
    } else {
      /* issue new write */
      wCur = axi_op;

      memcpy(ports.awaddr, &wCur->addr, (ports.addrBits + 7) / 8);
      wAAck = ports.awready;
      ports.awvalid = 1;

      uint64_t port_width = (ports.dataBits + 7) / 8;
      uint64_t data_offset =
          wCur->addr % port_width;  // for unaligned transfers, we must write
                                    // at this offset to data port
      memcpy(static_cast<uint8_t *>(ports.wdata) + data_offset, &wCur->value,
             std::min(port_width - data_offset, sizeof(wCur->value)));
      static_assert(sizeof(wCur->value) == 4,
                    "the following line assumes a fixed data length of 32 bit");
      (static_cast<uint8_t *>(ports.wstrb))[data_offset] = 0xF;
      wDAck = false;
      ports.wvalid = 0;

      wBAck = false;
      ports.bready = 1;
    }
  }
}