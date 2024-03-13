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
#include "axi.hh"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>

#define AXI_DEBUG 1

inline uint64_t pow2(uint64_t exponent) {
  return 1 << exponent;
}

void AXIReader::step(uint64_t cur_ts) {
  main_time_ = cur_ts;
  *addrP_.ready = 1;
  if (*addrP_.valid) {
    uint64_t axi_id = 0;
    std::memcpy(&axi_id, addrP_.id, (addrP_.id_bits + 7) / 8);

    uint64_t addr = 0;
    std::memcpy(&addr, addrP_.addr, (addrP_.addr_bits + 7) / 8);

    uint64_t step_size = pow2(*addrP_.size);
    assert(*addrP_.burst == 1 && "we currently only support INCR bursts");
    AXIOperationT *axi_op = new AXIOperationT(
        addr, step_size * (*addrP_.len + 1), axi_id, step_size, this);
#if AXI_DEBUG
    std::cerr << main_time_ << " AXI R: new op=" << axi_op
              << " addr=" << axi_op->addr << " len=" << axi_op->len
              << " id=" << axi_op->id << "\n";
#endif
    doRead(axi_op);
  }

  if (!curOp_ && !pending_.empty()) {
    curOp_ = pending_.front();
    curOff_ = 0;
    pending_.pop_front();
#if AXI_DEBUG
    std::cerr << main_time_ << " AXI R: starting response op=" << curOp_
              << " ready=" << static_cast<unsigned>(*dataP_.ready)
              << " id=" << curOp_->id << "\n";
#endif

    uint64_t data_bytes = (dataP_.data_bits + 7) / 8;
    size_t align = curOp_->addr % data_bytes;
    size_t num_bytes = std::min(data_bytes - align, curOp_->step_size);
    std::memcpy(static_cast<uint8_t *>(dataP_.data) + align, curOp_->buf,
                num_bytes);
    std::memcpy(dataP_.id, &curOp_->id, (dataP_.id_bits + 7) / 8);
    *dataP_.valid = 1;

    curOff_ += num_bytes;
    *dataP_.last = (curOff_ == curOp_->len);

    if (*dataP_.last) {
#if AXI_DEBUG
      std::cerr << main_time_ << " AXI R: completed_ op=" << curOp_
                << " id=" << curOp_->id << "\n";
#endif
      delete curOp_;
      curOp_ = nullptr;
    }
  } else if (curOp_ && *dataP_.ready) {
#if AXI_DEBUG
    std::cerr << main_time_ << " AXI R: step op=" << curOp_
              << " off=" << curOff_ << " id=" << curOp_->id << "\n";
#endif
    uint64_t data_bytes = (dataP_.data_bits + 7) / 8;
    size_t align = (curOp_->addr + curOff_) % data_bytes;
    size_t num_bytes = std::min(data_bytes - align, curOp_->step_size);
    std::memcpy(dataP_.data, curOp_->buf + curOff_, num_bytes);

    curOff_ += num_bytes;
    *dataP_.last = (curOff_ == curOp_->len);
    if (*dataP_.last) {
#if AXI_DEBUG
      std::cerr << main_time_ << " AXI R: completed_ op=" << curOp_
                << " id=" << curOp_->id << "\n";
#endif
      delete curOp_;
      curOp_ = nullptr;
    }
  } else if (!curOp_) {
    *dataP_.valid = 0;
    *dataP_.last = 0;
    std::memset(dataP_.data, 0, (dataP_.data_bits + 7) / 8);
    std::memset(dataP_.id, 0, (dataP_.id_bits + 7) / 8);
  }
}

void AXIReader::readDone(AXIOperationT *axi_op) {
#if AXI_DEBUG
  std::cerr << main_time_ << " AXI R: enqueue op=" << axi_op << "\n";
  std::cerr << "    " << std::hex;
  for (size_t i = 0; i < axi_op->len; i += sizeof(uint64_t)) {
    uint64_t dbg_value = 0;
    std::memcpy(&dbg_value, axi_op->buf + i,
                std::min(sizeof(uint64_t), axi_op->len - i));
    std::cerr << dbg_value << " ";
  }
  std::cerr << "\n" << std::dec;
#endif
  pending_.push_back(axi_op);
}

void AXIWriter::step(uint64_t cur_ts) {
  main_time_ = cur_ts;

  if (complOp_ && (*respP_.ready || complWasReady_)) {
#if AXI_DEBUG
    std::cerr << main_time_ << " AXI W: complete op=" << complOp_ << "\n";
#endif
    delete complOp_;
    complOp_ = nullptr;
    *respP_.valid = 0;
  }

  if (!complOp_ && !completed_.empty()) {
    complOp_ = completed_.front();
    completed_.pop_front();

#if AXI_DEBUG
    std::cerr << main_time_ << " AXI W: issuing completion op=" << complOp_
              << "\n";
#endif

    std::memcpy(respP_.id, &complOp_->id, (respP_.id_bits + 7) / 8);
    *respP_.valid = 1;
    complWasReady_ = *respP_.ready;
  }

  *addrP_.ready = 1;
  if (*addrP_.valid) {
    uint64_t axi_id = 0;
    std::memcpy(&axi_id, addrP_.id, (addrP_.id_bits + 7) / 8);

    uint64_t addr = 0;
    std::memcpy(&addr, addrP_.addr, (addrP_.addr_bits + 7) / 8);

    uint64_t step_size = pow2(*addrP_.size);
    assert(*addrP_.burst == 1 && "we currently only support INCR bursts");
    AXIOperationT *axi_op = new AXIOperationT(
        addr, step_size * (*addrP_.len + 1), axi_id, step_size, this);
#if AXI_DEBUG
    std::cerr << main_time_ << " AXI W: new op=" << axi_op
              << " addr=" << axi_op->addr << " len=" << axi_op->len
              << " id=" << axi_op->id << "\n";
#endif
    if (std::find_if(pending_.begin(), pending_.end(),
                     [axi_id](AXIOperationT *axi_op) {
                       return axi_op->id == axi_id;
                     }) != pending_.end()) {
      std::cerr << "AXI W id " << axi_id << " is already pending_"
                << "\n";
      abort();
    }
    pending_.emplace_back(axi_op);
  }

  *dataP_.ready = 1;
  if (*dataP_.valid) {
    if (pending_.empty()) {
      std::cerr << "AXI W pending_ shouldn't be empty"
                << "\n";
      abort();
    }

    AXIOperationT *axi_op = pending_.front();

#if AXI_DEBUG
    std::cerr << main_time_ << " AXI W: data id=" << axi_op->id
              << " op=" << axi_op
              << " last=" << static_cast<unsigned>(*dataP_.last) << "\n";
#endif

    uint64_t data_bytes = (dataP_.data_bits + 7) / 8;
    size_t align = (axi_op->addr + axi_op->off) % data_bytes;
#if AXI_DEBUG
    std::cerr << "AXI W: align=" << align << " off=" << axi_op->off
              << " step_size=" << axi_op->step_size << "\n";
#endif
    size_t num_bytes = std::min(data_bytes - align, axi_op->step_size);
    std::memcpy(axi_op->buf + axi_op->off,
                static_cast<uint8_t *>(dataP_.data) + align, num_bytes);
    axi_op->off += num_bytes;
    if (axi_op->off > axi_op->len) {
      std::cerr << "AXI W operation too long?"
                << "\n";
      abort();
    } else if (axi_op->off == axi_op->len) {
      if (!*dataP_.last) {
        std::cerr << "AXI W operation is done but last is not set?"
                  << "\n";
        abort();
      }

      pending_.pop_front();
      doWrite(axi_op);
    }
  }
}

void AXIWriter::writeDone(AXIOperationT *axi_op) {
#if AXI_DEBUG
  std::cerr << main_time_ << " AXI W: completed_ write for op=" << axi_op
            << "\n";
#endif
  completed_.push_back(axi_op);
}
