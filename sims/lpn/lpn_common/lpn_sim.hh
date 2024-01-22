#ifndef __LPN_SIM__
#define __LPN_SIM__
#include <bits/stdint-uintn.h>
#include "place_transition.hh"
#define LOOP_TS(func, t_size) for(int i=0;i < t_size; i++){ \
        Transition* t = t_list[i]; \
        func; \
    } 

uint64_t NextCommitTime(Transition* t_list[], int size);

int CommitAtTime(Transition* t_list[], int size, uint64_t time);

#endif