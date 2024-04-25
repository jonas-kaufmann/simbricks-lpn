#pragma once
#include <bits/stdint-uintn.h>
#include <assert.h>
#include <iostream>
#include "sims/lpn/vta/lpn_def/token_types.hh"
#include "../include/vta/hw_spec.h"
#include "sims/lpn/lpn_common/place_transition.hh"
#include "sims/lpn/vta/include/vta/driver.h"
#include "sims/lpn/vta/lpn_def/all_enum.hh"

template <int kBits, int kLane, int kMaxNumElem>
struct SRAM_{
  static const int kElemBytes = (kBits * kLane + 7) / 8;
};

template <int target_bits, int kLane>
struct Store_{
  const int targetWidth = (target_bits * kLane + 7) / 8;
  const int _kLane = kLane;
};

struct MemOpAddrUnit{
  SRAM_<VTA_INP_WIDTH, VTA_BATCH * VTA_BLOCK_IN, VTA_INP_BUFF_DEPTH> inp_; 
  SRAM_<VTA_WGT_WIDTH, VTA_BLOCK_IN * VTA_BLOCK_OUT, VTA_WGT_BUFF_DEPTH> wgt_;
  SRAM_<VTA_ACC_WIDTH, VTA_BATCH * VTA_BLOCK_OUT, VTA_ACC_BUFF_DEPTH> acc_;
  SRAM_<VTA_UOP_WIDTH, 1, VTA_UOP_BUFF_DEPTH> uop_;
  Store_<VTA_OUT_WIDTH, VTA_BATCH * VTA_BLOCK_OUT> store_;
};

MemOpAddrUnit memOpHelper;

int GetMemOpAddr(int tag, uint64_t insn_ptr, std::vector<uint64_t>& addrList, std::vector<uint32_t>& sizeList) {
  
  VTAGenericInsn* insn = reinterpret_cast<VTAGenericInsn*>(insn_ptr);
  union VTAInsn c;
  c.generic = *insn;
  uint64_t dram_base = c.mem.dram_base;
  int kElemBytes = 0;
  uint64_t base_addr = 0;
  if (tag == LOAD_INP_ID) {
    base_addr = reinterpret_cast<uint64_t>(dram_base * memOpHelper.inp_.kElemBytes);
    kElemBytes = memOpHelper.inp_.kElemBytes;
  } else if (tag == LOAD_WGT_ID) {
    base_addr = reinterpret_cast<uint64_t>(dram_base * memOpHelper.wgt_.kElemBytes);
    kElemBytes = memOpHelper.wgt_.kElemBytes;
  } else if (tag == LOAD_ACC_ID) {
    base_addr = reinterpret_cast<uint64_t>(dram_base * memOpHelper.acc_.kElemBytes);
    kElemBytes = memOpHelper.acc_.kElemBytes;
  } else if (tag == LOAD_UOP_ID) {
    kElemBytes = memOpHelper.uop_.kElemBytes;
    base_addr = reinterpret_cast<uint64_t>(dram_base * memOpHelper.uop_.kElemBytes);
  } else if (tag == STORE_ID){
    base_addr = reinterpret_cast<uint64_t>(dram_base * memOpHelper.store_.targetWidth);
  }

  if (tag == LOAD_INP_ID || tag == LOAD_WGT_ID || tag == LOAD_ACC_ID || tag == LOAD_UOP_ID) {
    int i = 0;
    for (uint32_t y = 0; y < c.mem.y_size; ++y) {
      uint64_t addr = base_addr + i;
      uint32_t size = kElemBytes * c.mem.x_size;
      addrList.push_back(addr);
      sizeList.push_back(size);
      i += kElemBytes * c.mem.x_stride;
    }
  } else if (tag == STORE_ID) {
    uint32_t len = ((c.mem.y_size - 1) * c.mem.x_stride + c.mem.x_size - 1) * memOpHelper.store_._kLane + memOpHelper.store_._kLane;
    addrList.push_back(base_addr);
    sizeList.push_back(len);
  }
  // std::cerr << "GetMemOpAddr: " << tag << " " << addrList.size() << " " << sizeList.size() << std::endl;
  return 0;
}

BaseToken* MakeLaunchToken() {
  NEW_TOKEN(token_class_total_insn, launch_token);
  launch_token->total_insn = 1;
  return launch_token;
}

BaseToken* MakeNumInsnToken(void* buffer) {

  VTAGenericInsn* insn = reinterpret_cast<VTAGenericInsn*>(buffer);
  union VTAInsn c;

  // Iterate over all instructions
  NEW_TOKEN(token_class_ostxyuullupppp, new_token);
  new_token->insn.data1_ = *(reinterpret_cast<uint64_t*>(insn));
  new_token->insn.data2_ = *(reinterpret_cast<uint64_t*>(insn)+1);
  new_token->opcode = static_cast<int>(ALL_ENUM::EMPTY);
  new_token->subopcode = static_cast<int>(ALL_ENUM::EMPTY);
  new_token->tstype = static_cast<int>(ALL_ENUM::EMPTY);
  new_token->pop_prev = 0;
  new_token->pop_next = 0;
  new_token->push_prev = 0;
  new_token->push_next = 0;
  new_token->xsize = 0;
  new_token->ysize = 0;

  c.generic = *insn;
  // c.generic = *(reinterpret_cast<VTAGenericInsn*>(&(new_token->insn)));
  // don't even care if its mem insn
  if (c.mem.opcode == VTA_OPCODE_LOAD || c.mem.opcode == VTA_OPCODE_STORE) {
    if (c.mem.x_size == 0) {
      if (c.mem.opcode == VTA_OPCODE_STORE) {
        new_token->opcode = static_cast<int>(ALL_ENUM::STORE);
        new_token->subopcode = static_cast<int>(ALL_ENUM::SYNC);
      } else if (c.mem.memory_type == VTA_MEM_ID_ACC ||
                 c.mem.memory_type == VTA_MEM_ID_ACC_8BIT ||
                 c.mem.memory_type == VTA_MEM_ID_UOP) {
        // printf("NOP-COMPUTE-STAGE\n");
        new_token->opcode = static_cast<int>(ALL_ENUM::COMPUTE);
        new_token->subopcode = static_cast<int>(ALL_ENUM::SYNC);
      } else {
        new_token->opcode = static_cast<int>(ALL_ENUM::LOAD);
        new_token->subopcode = static_cast<int>(ALL_ENUM::SYNC);
      }
      new_token->pop_prev = static_cast<int>(c.mem.pop_prev_dep);
      new_token->pop_next = static_cast<int>(c.mem.pop_next_dep);
      new_token->push_prev = static_cast<int>(c.mem.push_prev_dep);
      new_token->push_next = static_cast<int>(c.mem.push_next_dep);
      return new_token;
    }
    // Print instruction field information
    if (c.mem.opcode == VTA_OPCODE_LOAD) {
      // printf("LOAD ");
      if (c.mem.memory_type == VTA_MEM_ID_UOP) {
        new_token->opcode = static_cast<int>(ALL_ENUM::COMPUTE);
        new_token->subopcode = static_cast<int>(ALL_ENUM::LOADUOP);
      }
      if (c.mem.memory_type == VTA_MEM_ID_WGT) {
        new_token->opcode = static_cast<int>(ALL_ENUM::LOAD);
        new_token->subopcode = static_cast<int>(ALL_ENUM::LOAD);
        new_token->tstype = static_cast<int>(ALL_ENUM::WGT);
      }
      if (c.mem.memory_type == VTA_MEM_ID_INP) {
        new_token->opcode = static_cast<int>(ALL_ENUM::LOAD);
        new_token->subopcode = static_cast<int>(ALL_ENUM::LOAD);
        new_token->tstype = static_cast<int>(ALL_ENUM::INP);
      }
      if (c.mem.memory_type == VTA_MEM_ID_ACC) {
        new_token->opcode = static_cast<int>(ALL_ENUM::COMPUTE);
        new_token->subopcode = static_cast<int>(ALL_ENUM::LOADACC);
      }
    }
    if (c.mem.opcode == VTA_OPCODE_STORE) {
      new_token->opcode = static_cast<int>(ALL_ENUM::STORE);
      new_token->subopcode = static_cast<int>(ALL_ENUM::STORE);
    }

    new_token->pop_prev = static_cast<int>(c.mem.pop_prev_dep);
    new_token->pop_next = static_cast<int>(c.mem.pop_next_dep);
    new_token->push_prev = static_cast<int>(c.mem.push_prev_dep);
    new_token->push_next = static_cast<int>(c.mem.push_next_dep);
    new_token->xsize = static_cast<int>(c.mem.x_size);
    new_token->ysize = static_cast<int>(c.mem.y_size);

  } else if (c.mem.opcode == VTA_OPCODE_GEMM) {
    // Print instruction field information
    new_token->opcode = static_cast<int>(ALL_ENUM::COMPUTE);
    new_token->subopcode = static_cast<int>(ALL_ENUM::GEMM);

    new_token->pop_prev = static_cast<int>(c.mem.pop_prev_dep);
    new_token->pop_next = static_cast<int>(c.mem.pop_next_dep);
    new_token->push_prev = static_cast<int>(c.mem.push_prev_dep);
    new_token->push_next = static_cast<int>(c.mem.push_next_dep);
    new_token->xsize = static_cast<int>(c.mem.x_size);
    new_token->ysize = static_cast<int>(c.mem.y_size);
    new_token->uop_begin = static_cast<int>(c.gemm.uop_bgn);
    new_token->uop_end = static_cast<int>(c.gemm.uop_end);
    new_token->lp_1 = static_cast<int>(c.gemm.iter_out);
    new_token->lp_0 = static_cast<int>(c.gemm.iter_in);

  } else if (c.mem.opcode == VTA_OPCODE_ALU) {
    new_token->opcode = static_cast<int>(ALL_ENUM::COMPUTE);
    new_token->subopcode = static_cast<int>(ALL_ENUM::ALU);

    new_token->pop_prev = static_cast<int>(c.mem.pop_prev_dep);
    new_token->pop_next = static_cast<int>(c.mem.pop_next_dep);
    new_token->push_prev = static_cast<int>(c.mem.push_prev_dep);
    new_token->push_next = static_cast<int>(c.mem.push_next_dep);
    new_token->xsize = static_cast<int>(c.mem.x_size);
    new_token->ysize = static_cast<int>(c.mem.y_size);
    new_token->uop_begin = static_cast<int>(c.alu.uop_bgn);
    new_token->uop_end = static_cast<int>(c.alu.uop_end);
    new_token->lp_1 = static_cast<int>(c.alu.iter_out);
    new_token->lp_0 = static_cast<int>(c.alu.iter_in);
    new_token->use_alu_imm = static_cast<int>(c.alu.use_imm);

  } else if (c.mem.opcode == VTA_OPCODE_FINISH) {
    new_token->opcode = static_cast<int>(ALL_ENUM::LOAD);
    new_token->subopcode = static_cast<int>(ALL_ENUM::SYNC);
    new_token->tstype = static_cast<int>(ALL_ENUM::FINISH);
  }
  return new_token;
}