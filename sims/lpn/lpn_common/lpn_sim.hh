#ifndef __LPN_SIM__
#define __LPN_SIM__
#include "place_transition.hh"
#define LOOP_TS(func, t_size) for(int i=0;i < t_size; i++){ \
        Transition* t = t_list[i]; \
        func; \
    } 

int NextCommitTime(Transition* t_list[], int size);

int CommitAtTime(Transition* t_list[], int size, int time);

#endif