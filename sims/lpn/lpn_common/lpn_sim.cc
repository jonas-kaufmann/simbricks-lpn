#include "lpn_sim.hh"

uint64_t NextCommitTime(Transition* t_list[], int size){
    LOOP_TS(trigger(t), size);
    return min_time_g(t_list, size);
    // if(time == lpn::LARGE){
    //     // no more event
    //     return lpn::LARGE;
    // }
    // return time;
}

int CommitAtTime(Transition* t_list[], int size, uint64_t time){
    LOOP_TS(sync(t, time), size);
    return 0;
}