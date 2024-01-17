#include "lpn_sim.hh"

int NextCommitTime(Transition* t_list[], int size){
    LOOP_TS(trigger(t), size);
    int time = min_time_g(t_list, size);
    if(time == lpn::LARGE){
        // no more event
        return 0;
    }
    return 0;
}

int CommitAtTime(Transition* t_list[], int size, int time){
    LOOP_TS(sync(t, time), size);
    return 0;
}