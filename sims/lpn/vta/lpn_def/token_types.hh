#ifndef __TOKEN_TYPES__
#define __TOKEN_TYPES__
#include "sims/lpn/lpn_common/place_transition.hh"

CREATE_TOKEN_TYPE(
token_class_insn_count,
int insn_count=0;
)

CREATE_TOKEN_TYPE(
token_class_ostxyuullupppp,
int opcode=0;
int subopcode=0;
int tstype=0;
int xsize=0;
int ysize=0;
int uop_begin=0;
int uop_end=0;
int lp_1=0;
int lp_0=0;
int use_alu_imm=0;
int pop_prev=0;
int pop_next=0;
int push_prev=0;
int push_next=0;
)

CREATE_TOKEN_TYPE(
token_class_total_insn,
int total_insn=0;
)



#endif