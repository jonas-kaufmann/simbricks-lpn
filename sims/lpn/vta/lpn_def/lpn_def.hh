#ifndef __VTA_LPN_DEF__
#define __VTA_LPN_DEF__
#include <iostream>
#include "sims/lpn/lpn_common/place_transition.hh"
#include "transitions.hh"
#include "places.hh"

#define T_SIZE 13
#define P_SIZE 23
Transition* t_list[T_SIZE] = { &tstart, &t13, &t9, &t12, &t14, &t15, &t16, &tload_launch, &tload_done, &tstore_launch, &tstore_done, &tcompute_launch, &tcompute_done };
BasePlace* p_list[P_SIZE] = {
  &pstart, 
  &pcontrol_prime, 
  &pcontrol, 
  &psReadCmd, 
  &pnumInsn, 
  &psDrain, 
  &plaunch, 
  &pload_cap, 
  &pload_inst_q, 
  &pcompute_cap, 
  &pcompute_inst_q, 
  &pstore_cap, 
  &pstore_inst_q, 
  &pstore2compute, 
  &pload2compute, 
  &pcompute_process, 
  &pcompute2store, 
  &pstore_process, 
  &pcompute2load, 
  &pload_process, 
  &pcompute_done, 
  &pstore_done, 
  &pload_done 
};


void create_empty_queue(QT_type(EmptyToken*)* tokens, int num ){
  for(int i=0;i<num;i++){
    NEW_TOKEN(EmptyToken, x)
    tokens->push_back(x);
  }
}

static int total_insn = 0;
int lpn_finished(){
  if (pload_done.tokensLen()+pstore_done.tokensLen()+pcompute_done.tokensLen() == total_insn) {
    total_insn = 0;
    return 1;
  }
  return 0;
}

static bool lpn_started = false;

void lpn_end(){
  lpn_started = false;
}

void lpn_reset(){
  std::cerr << "lpn_reset" << std::endl;
  for (int i = 0; i < P_SIZE; i++) {
    p_list[i]->reset();
  }
  create_empty_queue(&(pcompute_cap.tokens), 512);  
  create_empty_queue(&(pload_cap.tokens), 512);  
  create_empty_queue(&(pstore_cap.tokens), 512);  
  create_empty_queue(&(pcontrol.tokens), 1);  
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
  assert(lpn_started == false);
  lpn_started = true;
  total_insn = insn_count;
  // for (int i = 0; i < insn_count; i++) {
  //   NEW_TOKEN(token_start, new_token);
  //   new_token->addr = addr + i * insn_size;
  //   new_token->insn_size = insn_size;
  //   pstart.pushToken(new_token);
  // }
  // fetching 8 insn at a time
  NEW_TOKEN(token_start, new_token);
  new_token->addr = addr;
  new_token->insn_size = insn_size*insn_count;
  pstart.pushToken(new_token);

  // for (int i = 0; i < insn_count; i=i+8) {
  //   NEW_TOKEN(token_start, new_token);
  //   int fetch_cnt = std::min(int(insn_count - i), 8);
  //   new_token->addr = addr + i * insn_size;
  //   new_token->insn_size = insn_size*fetch_cnt;
  //   pstart.pushToken(new_token);
  // }

  if (pload2compute.tokensLen() > 0) {
    std::cerr << "lpn_started with: pload2compute " << pload2compute.tokensLen() << std::endl;
  }
  
  if (pcompute2load.tokensLen() > 0) {
    std::cerr << "lpn_started with: pcompute2load " << pcompute2load.tokensLen() << std::endl;
  }

  if (pcompute2store.tokensLen() > 0) {
    std::cerr << "lpn_started with: pcompute2store " << pcompute2store.tokensLen() << std::endl;
  }

  if (pstore2compute.tokensLen() > 0) {
    std::cerr << "lpn_started with: pstore2compute " << pstore2compute.tokensLen() << std::endl;
  }

  pload_done.reset();
  pstore_done.reset();
  pcompute_done.reset();
}

#endif