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
    for(int i=0;i < size; i++){
        Transition* t = t_list[i];
        int fired = sync(t, time);
        // if(fired == 1){
            // printf("@%ld sync t done: %s\n", time/1000000, t->id.c_str());
        // }
    } 
    
    return 0;
}

void UpdateClk(Transition* t_list[], int size, uint64_t clk){
  for(int i=0; i<size; i++){
      Transition* t = t_list[i];
      t->time = clk;
  }
}
    
void TransitionCountLog(Transition* t_list[], int size){
  // for(int i=0; i<size; i++){
      // Transition* t = t_list[i];
      //std::cerr << "Transition:"<< t->id << " commit count=" << t->count << "\n";
  // }
}
