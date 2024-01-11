#ifndef __LPN_SIM__
#define __LPN_SIM__
#include "place_transition.hh"
#define LOOP_TS(func, t_size) for(int i=0;i < t_size; i++){ \
        transition* t = t_list[i]; \
        func; \
    } 

int NextCommitTime(transition* t_list[], int size){
    LOOP_TS(trigger(t), size);
    int time = min_time_g(t_list, size);
    if(time == LARGE){
        // no more event
        return 0;
    }
    return 0;
}

int CommitAtTime(transition* t_list[], int size, int time){
    LOOP_TS(sync(t, time), size);
    return 0;
}
#endif