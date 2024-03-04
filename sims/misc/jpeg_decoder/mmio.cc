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

#include "include/mmio.hh"

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