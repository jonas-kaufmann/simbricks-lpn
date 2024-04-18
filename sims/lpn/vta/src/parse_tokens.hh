#pragma once
#include "../include/vta/hw_spec.h"
#include "sims/lpn/lpn_common/place_transition.hh"


BaseToken* MakeLaunchToken() {
  NEW_TOKEN(token_class_total_insn, launch_token);
  launch_token->total_insn = 1;
  return launch_token;
}

BaseToken* MakeNumInsnToken(void* buffer, uint64_t loaded) {
  VTAGenericInsn* insn = reinterpret_cast<VTAGenericInsn*>(buffer) + loaded;  // TODO verify correctness
  union VTAInsn c;
  // Iterate over all instructions
  NEW_TOKEN(token_class_ostxyuullupppp, new_token);
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
  if (c.mem.opcode == VTA_OPCODE_LOAD || c.mem.opcode == VTA_OPCODE_STORE) {
    if (c.mem.x_size == 0) {
      if (c.mem.opcode == VTA_OPCODE_STORE) {
        // printf("NOP-STORE-STAGE\n");
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