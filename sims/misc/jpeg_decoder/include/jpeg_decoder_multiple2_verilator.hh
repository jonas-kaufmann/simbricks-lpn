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
#include <Vjpeg_decoder_multiple2.h>
#include <string.h>
#include <verilated.h>
#include <verilated_vcd_c.h>

#include <cstdint>
#include <functional>
#include <iostream>
#include <utility>

#include "axi.hh"

// interfaces with AXI4 master port that JPEG decoder block uses for DMA
class JpegDecoderMemReader : public AXIReader {
 protected:
  static AXIChannelReadAddr *addrPort(Vjpeg_decoder_multiple2 &top) {
    AXIChannelReadAddr *cra = new AXIChannelReadAddr;
    cra->addr_bits = 32;
    cra->id_bits = 6;
    cra->user_bits = 0;

    cra->ready = &top.saxigp2_arready;
    cra->valid = &top.saxigp2_arvalid;
    cra->addr = &top.saxigp2_araddr;
    cra->id = &top.saxigp2_arid;
    cra->user = &top.saxigp2_aruser;
    cra->len = &top.saxigp2_arlen;
    cra->size = &top.saxigp2_arsize;
    cra->burst = &top.saxigp2_arburst;
    cra->lock = &top.saxigp2_arlock;
    cra->cache = &top.saxigp2_arcache;
    cra->prot = &top.saxigp2_arprot;
    cra->qos = &top.saxigp2_arqos;
    return cra;
  }

  static AXIChannelReadData *dataPort(Vjpeg_decoder_multiple2 &top) {
    AXIChannelReadData *crd = new AXIChannelReadData;
    crd->data_bits = 128;
    crd->id_bits = 6;
    crd->user_bits = 0;

    crd->ready = &top.saxigp2_rready;
    crd->valid = &top.saxigp2_rvalid;
    crd->data = &top.saxigp2_rdata;
    crd->resp = &top.saxigp2_rresp;
    crd->last = &top.saxigp2_rlast;
    crd->id = &top.saxigp2_rid;
    return crd;
  }

  void doRead(AXIOperationT *op) override;

 public:
  explicit JpegDecoderMemReader(Vjpeg_decoder_multiple2 &top)
      : AXIReader(*addrPort(top), *dataPort(top)) {
  }
};

// interfaces with AXI4 master port that JPEG decoder block uses for DMA
class JpegDecoderMemWriter : public AXIWriter {
 protected:
  static AXIChannelWriteAddr *addrPort(Vjpeg_decoder_multiple2 &top) {
    AXIChannelWriteAddr *cwa = new AXIChannelWriteAddr;
    cwa->addr_bits = 32;
    cwa->id_bits = 6;
    cwa->user_bits = 0;

    cwa->ready = &top.saxigp2_awready;
    cwa->valid = &top.saxigp2_awvalid;
    cwa->addr = &top.saxigp2_awaddr;
    cwa->id = &top.saxigp2_awid;
    cwa->user = &top.saxigp2_awuser;
    cwa->len = &top.saxigp2_awlen;
    cwa->size = &top.saxigp2_awsize;
    cwa->burst = &top.saxigp2_awburst;
    cwa->lock = &top.saxigp2_awlock;
    cwa->cache = &top.saxigp2_awcache;
    cwa->prot = &top.saxigp2_awprot;
    cwa->qos = &top.saxigp2_awqos;
    return cwa;
  }

  static AXIChannelWriteData *dataPort(Vjpeg_decoder_multiple2 &top) {
    AXIChannelWriteData *cwd = new AXIChannelWriteData;
    cwd->data_bits = 128;
    cwd->id_bits = 6;
    cwd->user_bits = 0;

    cwd->ready = &top.saxigp2_wready;
    cwd->valid = &top.saxigp2_wvalid;
    cwd->data = &top.saxigp2_wdata;
    cwd->strb = &top.saxigp2_wstrb;
    cwd->last = &top.saxigp2_wlast;
    return cwd;
  }

  static AXIChannelWriteResp *respPort(Vjpeg_decoder_multiple2 &top) {
    AXIChannelWriteResp *cwr = new AXIChannelWriteResp;
    cwr->id_bits = 8;
    cwr->user_bits = 1;

    cwr->ready = &top.saxigp2_bready;
    cwr->valid = &top.saxigp2_bvalid;
    cwr->resp = &top.saxigp2_bresp;
    cwr->id = &top.saxigp2_bid;

    return cwr;
  }

  void doWrite(AXIOperationT *op) override;

 public:
  explicit JpegDecoderMemWriter(Vjpeg_decoder_multiple2 &top)
      : AXIWriter(*addrPort(top), *dataPort(top), *respPort(top)) {
  }
};

// interfaces with JPEG decoder's AXI4 lite slave port for memory-mapped
// registers
class JpegDecoderMMIOInterface : public MMIOInterface {
  static MMIOPorts assignPorts(Vjpeg_decoder_multiple2 &top) {
    // set some required inputs on AXI slave interface, which aren't handles by
    // this AXI lite master interface
    top.maxigp0_awsize = 2;
    top.maxigp0_arsize = 2;
    top.maxigp0_wlast = 1;

    return MMIOPorts{.addrBits = 32,
                     .dataBits = 128,
                     .awaddr = &top.maxigp0_awaddr,
                     .awprot = top.maxigp0_awprot,
                     .awvalid = top.maxigp0_awvalid,
                     .awready = top.maxigp0_awready,
                     .wdata = &top.maxigp0_wdata,
                     .wstrb = &top.maxigp0_wstrb,
                     .wvalid = top.maxigp0_wvalid,
                     .wready = top.maxigp0_wready,
                     .bresp = top.maxigp0_bresp,
                     .bvalid = top.maxigp0_bvalid,
                     .bready = top.maxigp0_bready,
                     .araddr = &top.maxigp0_araddr,
                     .arprot = top.maxigp0_arprot,
                     .arvalid = top.maxigp0_arvalid,
                     .arready = top.maxigp0_arready,
                     .rdata = &top.maxigp0_rdata,
                     .rresp = top.maxigp0_rresp,
                     .rvalid = top.maxigp0_rvalid,
                     .rready = top.maxigp0_rready};
  }

 public:
  JpegDecoderMMIOInterface(Vjpeg_decoder_multiple2 &top, mmioDoneT mmio_done_callback)
      : MMIOInterface(std::move(mmio_done_callback), assignPorts(top)) {
  }
};

// interfaces with clock gater's AXI4 lite slave port for memory-mapped
// registers
class ClockGaterMMIOInterface : public MMIOInterface {
  static MMIOPorts assignPorts(Vjpeg_decoder_multiple2 &top) {
    // set some required inputs on AXI slave interface, which aren't handles by
    // this AXI lite master interface
    top.maxigp0_awsize = 2;
    top.maxigp0_arsize = 2;
    top.maxigp0_wlast = 1;

    return MMIOPorts{.addrBits = 32,
                     .dataBits = 128,
                     .awaddr = &top.maxigp1_awaddr,
                     .awprot = top.maxigp1_awprot,
                     .awvalid = top.maxigp1_awvalid,
                     .awready = top.maxigp1_awready,
                     .wdata = &top.maxigp1_wdata,
                     .wstrb = &top.maxigp1_wstrb,
                     .wvalid = top.maxigp1_wvalid,
                     .wready = top.maxigp1_wready,
                     .bresp = top.maxigp1_bresp,
                     .bvalid = top.maxigp1_bvalid,
                     .bready = top.maxigp1_bready,
                     .araddr = &top.maxigp1_araddr,
                     .arprot = top.maxigp1_arprot,
                     .arvalid = top.maxigp1_arvalid,
                     .arready = top.maxigp1_arready,
                     .rdata = &top.maxigp1_rdata,
                     .rresp = top.maxigp1_rresp,
                     .rvalid = top.maxigp1_rvalid,
                     .rready = top.maxigp1_rready};
  }

 public:
  ClockGaterMMIOInterface(Vjpeg_decoder_multiple2 &top, mmioDoneT mmio_done_callback)
      : MMIOInterface(std::move(mmio_done_callback), assignPorts(top)) {
  }
};

void mmio_done(MMIOOp *op, uint64_t cur_ts);
