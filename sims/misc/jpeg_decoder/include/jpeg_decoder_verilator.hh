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
#pragma once
#include <Vjpeg_decoder.h>
#include <verilated.h>
#include <verilated_vcd_c.h>

#include <cstdint>
#include <utility>

#include <simbricks/rtl/axi/axi.hh>

#include "mmio.hh"

// interfaces with AXI4 master port that JPEG decoder block uses for DMA
class JpegDecoderMemReader : public AXIReader {
 protected:
  void doRead(AXIOperationT *axi_op) override;

 public:
  explicit JpegDecoderMemReader(Vjpeg_decoder &top) {
    // set up address port
    addrP_.addr_bits = 32;
    addrP_.id_bits = 4;

    addrP_.ready = &top.m_axi_arready;
    addrP_.valid = &top.m_axi_arvalid;
    addrP_.addr = &top.m_axi_araddr;
    addrP_.id = &top.m_axi_arid;
    addrP_.len = &top.m_axi_arlen;
    addrP_.size = &axi_size_;
    addrP_.burst = &top.m_axi_arburst;

    // set up data port
    dataP_.data_bits = 32;
    dataP_.id_bits = 4;

    dataP_.ready = &top.m_axi_rready;
    dataP_.valid = &top.m_axi_rvalid;
    dataP_.data = &top.m_axi_rdata;
    dataP_.resp = &top.m_axi_rresp;
    dataP_.last = &top.m_axi_rlast;
    dataP_.id = &top.m_axi_rid;
  }

 private:
  CData axi_size_ = 0b010;
};

// interfaces with AXI4 master port that JPEG decoder block uses for DMA
class JpegDecoderMemWriter : public AXIWriter {
 protected:
  void doWrite(AXIOperationT *axi_op) override;

 public:
  explicit JpegDecoderMemWriter(Vjpeg_decoder &top) {
    // set up address port
    addrP_.addr_bits = 32;
    addrP_.id_bits = 4;

    addrP_.ready = &top.m_axi_awready;
    addrP_.valid = &top.m_axi_awvalid;
    addrP_.addr = &top.m_axi_awaddr;
    addrP_.id = &top.m_axi_awid;
    addrP_.len = &top.m_axi_awlen;
    addrP_.size = &axi_size_;
    addrP_.burst = &top.m_axi_awburst;

    // set up data port
    dataP_.data_bits = 32;
    dataP_.id_bits = 4;

    dataP_.ready = &top.m_axi_wready;
    dataP_.valid = &top.m_axi_wvalid;
    dataP_.data = &top.m_axi_wdata;
    dataP_.strb = &top.m_axi_wstrb;
    dataP_.last = &top.m_axi_wlast;

    // set up response port
    respP_.id_bits = 4;

    respP_.ready = &top.m_axi_bready;
    respP_.valid = &top.m_axi_bvalid;
    respP_.resp = &top.m_axi_bresp;
    respP_.id = &top.m_axi_bid;
  }

 private:
  CData axi_size_ = 0b010;
};

// interfaces with JPEG decoder's AXI4 lite slave port for memory-mapped
// registers
class JpegDecoderMMIOInterface : public MMIOInterface {
  static MMIOPorts assignPorts(Vjpeg_decoder &top) {
    return MMIOPorts{32,
                     32,
                     &top.s_axil_awaddr,
                     top.s_axil_awvalid,
                     top.s_axil_awready,
                     &top.s_axil_wdata,
                     &top.s_axil_wstrb,
                     top.s_axil_wvalid,
                     top.s_axil_wready,
                     top.s_axil_bresp,
                     top.s_axil_bvalid,
                     top.s_axil_bready,
                     &top.s_axil_araddr,
                     top.s_axil_arvalid,
                     top.s_axil_arready,
                     &top.s_axil_rdata,
                     top.s_axil_rresp,
                     top.s_axil_rvalid,
                     top.s_axil_rready};
  }

 public:
  JpegDecoderMMIOInterface(Vjpeg_decoder &top, mmioDoneT mmio_done_callback)
      : MMIOInterface(std::move(mmio_done_callback), assignPorts(top)) {
  }
};

void mmio_done(MMIOOp *mmio_op, uint64_t cur_ts);
