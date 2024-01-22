#ifndef __JPEG_DECODER_LPN_DEF__
#define __JPEG_DECODER_LPN_DEF__
#include "sims/lpn/lpn_common/place_transition.hh"
#include "transitions.hh"
#include "places.hh"
#define T_SIZE 6 
Transition* t_list[T_SIZE] = {&t0, &t1, &t2, &t3, &t4, &t5};

void create_empty_queue(QT_type(EmptyToken*)* tokens, int num ){
  for(int i=0;i<num;i++){
    NEW_TOKEN(EmptyToken, x)
    tokens->push_back(x);
  }
}

void lpn_init(){
  static int init_done = 0;
  if(!init_done){
    init_done = 1;
    create_empty_queue(&(p4.tokens), 4);
    create_empty_queue(&(p5.tokens), 7);
    create_empty_queue(&(p6.tokens), 4);
    create_empty_queue(&(p8.tokens), 1);
    create_empty_queue(&(p11.tokens), 4);
    create_empty_queue(&(p20.tokens), 4);
  }
}
#endif