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
#include <bits/stdint-uintn.h>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>

#define AXI_R_DEBUG 0
#define AXI_W_DEBUG 0

inline uint64_t pow2(uint64_t exponent) {
  return 1 << exponent;
}

static uint64_t read_tmp[20] = {0};
static bool read_tmp_active[20] = {0};
static uint64_t write_tmp[20] = {0};
static bool write_tmp_active[20] = {0};

#define RegWrite(prefix, idx, signal, val) prefix##_tmp[idx] = val; prefix##_tmp_active[idx]=true
#define ResetTmp(prefix) for(int i=0;i<20;i++){prefix##_tmp_active[i]=false;}
#define RegApply(prefix, idx, signal) if(prefix##_tmp_active[idx]==true) {(signal) = prefix##_tmp[idx]; prefix##_tmp_active[idx]=false;}
#define RegGuard(prefix, idx) (prefix##_tmp_active[idx])
#define ActiveReset(prefix, idx) prefix##_tmp[idx] = false 
#define RegFetch(prefix, idx) (prefix##_tmp[idx])

void AXIReader::step(uint64_t cur_ts) {
  ResetTmp(read);
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
#if AXI_R_DEBUG
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
#if AXI_R_DEBUG
    std::cerr << main_time_ << " AXI R: starting response op=" << curOp_
              << " ready=" << static_cast<unsigned>(*dataP_.ready)
              << " id=" << curOp_->id << "\n";
#endif

    uint64_t data_bytes = (dataP_.data_bits + 7) / 8;
    size_t align = curOp_->addr % data_bytes;
    size_t num_bytes = std::min(data_bytes - align, curOp_->step_size);
    // std::memcpy(static_cast<uint8_t *>(dataP_.data) + align, curOp_->buf,
                // num_bytes);
    RegWrite(read, 15, dataP_.data, align);
    RegWrite(read, 16, dataP_.data, (uint64_t)curOp_->buf);
    RegWrite(read, 17, dataP_.data, num_bytes);
            
    // std::memcpy(dataP_.id, &curOp_->id, (dataP_.id_bits + 7) / 8);
    RegWrite(read, 13, dataP_.id, (uint64_t)(&curOp_->id));
    RegWrite(read, 14, dataP_.id, (dataP_.id_bits + 7) / 8);

    // if you set valid, you have to set the data correctly as well
    RegWrite(read, 0, *dataP_.valid, 1);
    // *dataP_.valid = 1;

    curOff_ += num_bytes;
    // *dataP_.last = (curOff_ == curOp_->len);
    RegWrite(read, 1, *dataP_.last, (curOff_ == curOp_->len));

    if (*dataP_.last) {
#if AXI_R_DEBUG
      std::cerr << main_time_ << " AXI R: completed_ op=" << curOp_
                << " id=" << curOp_->id << "\n";
#endif
      delete curOp_;
      curOp_ = nullptr;
      RegWrite(read, 2, *dataP_.valid, 0);
      // *dataP_.valid = 0;
      RegWrite(read, 3, *dataP_.last, 0);
      // *dataP_.last = 0;
    }
  } else if (curOp_ && *dataP_.ready && *dataP_.valid) {
    // the following is the next data segment
#if AXI_R_DEBUG
    std::cerr << main_time_ << " AXI R: step op=" << curOp_
              << " off=" << curOff_ << " id=" << curOp_->id << "\n";
#endif
    uint64_t data_bytes = (dataP_.data_bits + 7) / 8;
    size_t align = (curOp_->addr + curOff_) % data_bytes;
    size_t num_bytes = std::min(data_bytes - align, curOp_->step_size);
    // std::memcpy(dataP_.data, curOp_->buf + curOff_, num_bytes);
    RegWrite(read, 11, dataP_.data, (uint64_t)(curOp_->buf + curOff_));
    RegWrite(read, 12, dataP_.data, num_bytes);
    
    curOff_ += num_bytes;
    // *dataP_.last = (curOff_ == curOp_->len);
    RegWrite(read, 4, *dataP_.last, (curOff_ == curOp_->len));

    if (*dataP_.last) {
#if AXI_R_DEBUG
      std::cerr << main_time_ << " AXI R: completed_ op=" << curOp_
                << " id=" << curOp_->id << "\n";
#endif
      delete curOp_;
      curOp_ = nullptr;
      RegWrite(read, 5, *dataP_.valid, 0);
      // *dataP_.valid = 0;
      RegWrite(read, 6, *dataP_.last, 0);
      // *dataP_.last = 0;
    }
  } else if (!curOp_) {
    RegWrite(read, 7, *dataP_.valid, 0);
    // *dataP_.valid = 0;
    RegWrite(read, 8, *dataP_.last, 0);
    // *dataP_.last = 0;
    RegWrite(read, 9, *dataP_.data, (dataP_.data_bits + 7) / 8);
    RegWrite(read, 10, *dataP_.id, (dataP_.id_bits + 7) / 8);
    // std::memset(dataP_.data, 0, (dataP_.data_bits + 7) / 8);
    // std::memset(dataP_.id, 0, (dataP_.id_bits + 7) / 8);
  }
}

void AXIReader::readDone(AXIOperationT *axi_op) {
#if AXI_R_DEBUG
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
  ResetTmp(write);
  main_time_ = cur_ts;
  *addrP_.ready = 1;
  *dataP_.ready = 1;

  // if (complOp_ && (*respP_.ready || complWasReady_)) {
  if (*respP_.valid && *respP_.ready) {
#if AXI_W_DEBUG
    std::cerr << main_time_ << " AXI W: complete op=" << complOp_ << "\n";
#endif
    delete complOp_;
    complOp_ = nullptr;
    // *respP_.valid = 0;
    RegWrite(write, 0, *respP_.valid, 0);
  }

  if ( !(*respP_.valid) && !complOp_ && !completed_.empty()) {
    complOp_ = completed_.front();
    completed_.pop_front();

#if AXI_W_DEBUG
  std::cerr << main_time_ << " AXI W: issuing completion op=" << complOp_ 
            <<" bready=" << *respP_.ready <<" bvalid=" << *respP_.valid  << std::endl;

   
#endif

    // std::memcpy(respP_.id, &complOp_->id, (respP_.id_bits + 7) / 8);
    RegWrite(write, 2, respP_.id, (uint64_t)(&complOp_->id));
    RegWrite(write, 3, respP_.id, (respP_.id_bits + 7) / 8 );
    
    // *respP_.valid = 1;
    RegWrite(write, 1, *respP_.valid, 1);

  }

  if (*addrP_.valid && *addrP_.ready) {
    uint64_t axi_id = 0;
    std::memcpy(&axi_id, addrP_.id, (addrP_.id_bits + 7) / 8);

    uint64_t addr = 0;
    std::memcpy(&addr, addrP_.addr, (addrP_.addr_bits + 7) / 8);

    uint64_t step_size = pow2(*addrP_.size);
    assert(*addrP_.burst == 1 && "we currently only support INCR bursts");
    AXIOperationT *axi_op = new AXIOperationT(
        addr, step_size * (*addrP_.len + 1), axi_id, step_size, this);
#if AXI_W_DEBUG
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

  if (*dataP_.valid && *dataP_.ready) {
    if (pending_.empty()) {
      std::cerr << "AXI W pending_ shouldn't be empty"
                << "\n";
      abort();
    }

    AXIOperationT *axi_op = pending_.front();

#if AXI_W_DEBUG
    std::cerr << main_time_ << " AXI W: data id=" << axi_op->id
              << " op=" << axi_op
              << " last=" << static_cast<unsigned>(*dataP_.last) << "\n";
#endif

    uint64_t data_bytes = (dataP_.data_bits + 7) / 8;
    size_t align = (axi_op->addr + axi_op->off) % data_bytes;
#if AXI_W_DEBUG
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
#if AXI_W_DEBUG
    std::cerr << "AXI W: writeout" << " off=" << axi_op->off
              << " len=" << axi_op->len << "\n";
#endif
      pending_.pop_front();
      doWrite(axi_op);
    }
  }
}

void AXIWriter::writeDone(AXIOperationT *axi_op) {
#if AXI_W_DEBUG
  std::cerr << main_time_ << " AXI W: completed_ write for op=" << axi_op
            << "\n";
#endif
  completed_.push_back(axi_op);
}

void AXIWriter::step_apply(){
  RegApply(write, 0, *respP_.valid);
  RegApply(write, 1, *respP_.valid);

  if(RegGuard(write, 2) && RegGuard(write, 3)){
    std::memcpy(respP_.id, (void*) RegFetch(write, 2), RegFetch(write,3));
    ActiveReset(write, 2); ActiveReset(write, 3);
  }
}

void AXIReader::step_apply(){
  RegApply(read, 0, *dataP_.valid);
  RegApply(read, 1, *dataP_.last);
  RegApply(read, 2, *dataP_.valid);
  RegApply(read, 3, *dataP_.last);
  RegApply(read, 4, *dataP_.last);
  RegApply(read, 5, *dataP_.valid);
  RegApply(read, 6, *dataP_.last);
  RegApply(read, 7, *dataP_.valid);
  RegApply(read, 8, *dataP_.last);
  
  if(RegGuard(read, 9)){
    std::memset(dataP_.data, 0, RegFetch(read, 9));
    ActiveReset(read, 9);
  }

  if(RegGuard(read, 10)){
    std::memset(dataP_.id, 0, RegFetch(read, 10));
    ActiveReset(read, 10);
  }

  if(RegGuard(read, 11) && RegGuard(read, 12)){
    std::memcpy(dataP_.data, (void*)RegFetch(read, 11), RegFetch(read, 12));
    ActiveReset(read, 11); ActiveReset(read, 12);
  }

  if(RegGuard(read, 13) && RegGuard(read, 14)){
    std::memcpy(dataP_.id, (void*)RegFetch(read, 13), RegFetch(read, 14)); 
    ActiveReset(read, 13); ActiveReset(read, 14);
  }

  if(RegGuard(read, 15) && RegGuard(read, 16) && RegGuard(read, 17)){
   std::memcpy(static_cast<uint8_t *>(dataP_.data) + RegFetch(read, 15), (void*)(RegFetch(read, 16)),
                RegFetch(read, 17));  
    ActiveReset(read, 15); ActiveReset(read, 16); ActiveReset(read, 17);
  }
}