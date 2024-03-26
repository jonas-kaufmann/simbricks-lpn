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

void UpdateClk(Transition* t_list[], int size, uint64_t clk){
  for(int i=0; i<size; i++){
      Transition* t = t_list[i];
      t->time = clk;
  }
}
    
void TransitionCountLog(Transition* t_list[], int size){
  for(int i=0; i<size; i++){
      Transition* t = t_list[i];
      std::cerr << "Transition:"<< t->id << " commit count=" << t->count << "\n";
  }
}
