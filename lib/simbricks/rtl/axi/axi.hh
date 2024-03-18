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
#ifndef VERILATOR_AXI_ADAPTER_H_
#define VERILATOR_AXI_ADAPTER_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <type_traits>


struct AXIChannelReadAddr {
  // Signal widths
  size_t addr_bits;
  size_t id_bits;
  size_t user_bits;

  // Signals
  uint8_t *ready;  // <-
  uint8_t *valid;  // ->

  void *addr;     // ->
  void *id;       // ->
  void *user;     // ->
  uint8_t *len;     // ->
  uint8_t *size;    // ->
  uint8_t *burst;   // ->
  uint8_t *lock;    // ->
  uint8_t *cache;   // ->
  uint8_t *prot;    // ->
  uint8_t *qos;     // ->
  uint8_t *region;  // ->
};

struct AXIChannelReadData {
  // Signal widths
  size_t data_bits;
  size_t id_bits;
  size_t user_bits;

  // Signals
  uint8_t *ready;  // ->
  uint8_t *valid;  // <-

  uint8_t *resp;  // <-
  void *data;   // <-
  void *id;     // <-
  uint8_t *last;  // <-
};

struct AXIChannelWriteAddr {
  // Signal widths
  size_t addr_bits;
  size_t id_bits;
  size_t user_bits;

  // Signals
  uint8_t *ready;  // <-
  uint8_t *valid;  // ->

  void *addr;     // ->
  void *id;       // ->
  void *user;     // ->
  uint8_t *len;     // ->
  uint8_t *size;    // ->
  uint8_t *burst;   // ->
  uint8_t *lock;    // ->
  uint8_t *cache;   // ->
  uint8_t *prot;    // ->
  uint8_t *qos;     // ->
  uint8_t *region;  // ->
};

struct AXIChannelWriteData {
  // Signal widths
  size_t data_bits;
  size_t id_bits;
  size_t user_bits;

  // Signals
  uint8_t *ready;  // <-
  uint8_t *valid;  // ->

  void *data;   // ->
  void *strb;   // ->
  uint8_t *last;  // ->
};

struct AXIChannelWriteResp {
  // Signal widths
  size_t id_bits;
  size_t user_bits;

  // Signals
  uint8_t *ready;  // ->
  uint8_t *valid;  // <-

  uint8_t *resp;  // ->
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
  size_t off = 0;
  size_t step_size;

  using issuedByT = std::conditional_t<IsWrite, AXIWriter, AXIReader> *;
  issuedByT issued_by;

  AXIOperation(uint64_t addr_, size_t len_, uint64_t id_, size_t step_size,
               issuedByT issuedBy)
      : addr(addr_),
        len(len_),
        id(id_),
        buf(new uint8_t[len_]),
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
  uint64_t main_time_ = 0;
  AXIChannelReadAddr addrP_{};
  AXIChannelReadData dataP_{};

  std::deque<AXIOperationT *> pending_{};
  AXIOperationT *curOp_ = nullptr;
  size_t curOff_ = 0;

  virtual void doRead(AXIOperationT *axi_op) = 0;

 public:
  void readDone(AXIOperationT *axi_op);
  void step(uint64_t cur_ts);
  void step_apply();
};

class AXIWriter {
 public:
  using AXIOperationT = AXIOperation<true>;

 protected:
  uint64_t main_time_ = 0;
  AXIChannelWriteAddr addrP_{};
  AXIChannelWriteData dataP_{};
  AXIChannelWriteResp respP_{};

  std::deque<AXIOperationT *> pending_{};
  std::deque<AXIOperationT *> completed_{};
  AXIOperationT *complOp_ = nullptr;
  bool complWasReady_ = false;

  virtual void doWrite(AXIOperationT *axi_op) = 0;

 public:
  void writeDone(AXIOperationT *axi_op);
  void step(uint64_t cur_ts);
  void step_apply();
};

#endif
