#ifndef __VTA_LPN_DEF__
#define __VTA_LPN_DEF__
#include "sims/lpn/lpn_common/place_transition.hh"
#include "transitions.hh"
#include "places.hh"

#define T_SIZE 13
Transition* t_list[T_SIZE] = { &tstart, &t13, &t9, &t12, &t14, &t15, &t16, &tload_launch, &tload_done, &tstore_launch, &tstore_done, &tcompute_launch, &tcompute_done };

void create_empty_queue(QT_type(EmptyToken*)* tokens, int num ){
  for(int i=0;i<num;i++){
    NEW_TOKEN(EmptyToken, x)
    tokens->push_back(x);
  }
}

void lpn_init(){
  static int init_done = 0;
  if (init_done) assert(0);
  if(!init_done){
    init_done = 1;
    create_empty_queue(&(pcompute_cap.tokens), 512);  
    create_empty_queue(&(pload_cap.tokens), 512);  
    create_empty_queue(&(pstore_cap.tokens), 512);  
//   collect_insns(&(pnumInsn.tokens), benchmark);

    // NEW_TOKEN(token_class_total_insn, numInstToken);
    // numInstToken->total_insn = pnumInsn.tokens.size();
    // plaunch.tokens.push_back(numInstToken);
    create_empty_queue(&(pcontrol.tokens), 1);  
  }
}

// TODO define interface for start
void lpn_start(uint64_t addr, uint32_t insn_count, size_t insn_size) {
  NEW_TOKEN(token_start, new_token);
  new_token->addr = addr;
  new_token->insn_count = insn_count;
  new_token->insn_size = insn_size;
  new_token->loaded = 0;
  pstart.pushToken(new_token);
}

#endif