#ifndef __LPN_PLACE_TRANSITION__
#define __LPN_PLACE_TRANSITION__
#define N_ELEM 10
#define LARGE 1<<30
#include <string>
#include <map>
#include <set>
#include <deque>
#include <vector>
#include <iostream>
#include <functional>

#define QT_type(T) deque<T>
#define NEW_QT(T, x) QT_type(T)* x = new QT_type(T)
#define NEW_TOKEN(T, x) T* x = new T;

using namespace std;

class base_token {
public:
  virtual void print_token() {}
  virtual map<string, int>* as_dictionary(){
    return NULL;
  }
  virtual ~base_token() {}
  int ts=0;
};

class empty_token: public base_token{
  map<string, int>* as_dictionary(){
    return NULL;
  }
};

#define CREATE_TOKEN_TYPE(name, ...) \
class name: public base_token \
{ public: __VA_ARGS__  };

class base_place {
public:
    // deque<empty_token*> tokens;
    string id;
    base_place(string asid) : id(asid) {}
    virtual int tokens_len() const {
      return 0;
    }
    virtual int ts_at(int idx) const {
      return 0;
    }
    virtual void set_token_ts(int idx, int ts) {
      return ;
    }
    virtual string get_id() const {
      return "";
    }
    virtual void push_token(base_token*){
      cout << "call default push token" << endl;
      return ;
    }
    virtual void pop_token() {
      return;
    }
    virtual void reset() {
      return;
    }
    virtual void copy_to_init(){
      return;
    }
    virtual bool has_init() const {
      return 0;
    }
    virtual int init_size() const {
      return 0;
    }
    virtual base_token* init_at(int idx) const {
      return NULL;
    }
    virtual ~base_place() {}
};

template<typename Token_Type = empty_token>
struct place : public base_place
{
  public:
  place(string asid) : base_place(asid) {}
  deque<Token_Type*> tokens;
  deque<Token_Type*> tokens_init;
  
  bool has_init() const override{
    return tokens_init.size() > 0;
  }
  
  void copy_to_init() override{
    for(auto& token: tokens){
      tokens_init.push_back(token);
    }
  }

  int init_size() const override{
    return tokens_init.size();
  }

  base_token* init_at(int idx) const override{
    return tokens_init[idx];
  }

  int tokens_len() const override {
    return tokens.size();
  }
  int ts_at(int idx) const override{
    return tokens[idx]->ts;
  } 
  void set_token_ts(int idx, int ts) override{
    tokens[idx]->ts = ts;
  }
  string get_id() const override {
        return id;
  }
  void pop_token(){
    tokens.pop_front();
  }
  void push_token(base_token* token){
    // assert(token != NULL);
    tokens.push_back((Token_Type*)(token));
  }
  void reset() override{
      tokens.clear();
  }
};

#define create_input_vector_list() vector<base_place*> p_input; 
#define create_input_w_vector_list() std::function<int()> pi_w[N_ELEM]; 
#define create_input_guard_vector_list() std::function<bool()> pi_guard[N_ELEM]={NULL}; 
#define create_input_threshold_vector_list() int pi_w_threshold[N_ELEM];
#define create_output_vector_list() vector<base_place*> p_output; std::function<void(base_place*)> po_w[N_ELEM];

typedef struct transition
{
  string id;
  // int (*delay_f)();
  std::function<int()> delay_f;

  create_input_vector_list();
  create_input_w_vector_list();
  create_input_guard_vector_list();
  create_input_threshold_vector_list();
  create_output_vector_list();
  
  deque<int> consume_tokens;
  
  int delay_event=-1; //-1 if no event
  int disable = 0;
  int pip=-1; 
  int pip_ts = 0;
  int count=0;
  int time=0;
}transition;

int check_token_requirement(base_place* self, int num){
   if (num == -2)
     return self->tokens_len() == 0;
   return self->tokens_len() >= num;
}

void fire(base_place* self, int num){
   for(int i=0; i<num; i++){
      // if(self->tokens_len() == 0){
      //   assert(0);
      // }
      self->pop_token();
   }
}

int able_to_fire_t(transition* self, int& enabled_ts){

  int input_size= self->p_input.size(); 
  int max_ts = 0;
  for(int i = 0; i< input_size; i++){ 
     base_place* p = self->p_input[i]; 
     int consume_num_tokens_threshold = 0; 
     int consume_num_tokens_real = self->pi_w[i](); 
     if(self->pi_w_threshold[i] == 0)
        consume_num_tokens_threshold = consume_num_tokens_real; 
     else 
        consume_num_tokens_threshold = self->pi_w_threshold[i]; 
     self->consume_tokens.push_back(consume_num_tokens_real);
     if (! check_token_requirement(p, consume_num_tokens_threshold) ){ 
        self->consume_tokens.clear();
        return 0; 
     }
     max_ts = max(max_ts, p->ts_at(consume_num_tokens_threshold-1)); 
  }
  enabled_ts = max_ts;
  return 1;
} 

void fire_t(transition* self){
  
  //  cout << "fire_t " << "t"<<self->id << endl;
   int input_size= self->p_input.size(); 
   for(int i=0; i < input_size; i++){ 
     base_place* p = self->p_input[i]; 
     int consume_num_tokens = self->consume_tokens.front();
     self->consume_tokens.pop_front(); 
     fire(p, consume_num_tokens); 
   }

  self->consume_tokens.clear();

}

void accept_t(transition* self){
  // cout << "accept " << "t"<< self->id << endl;
  int output_size = self->p_output.size(); 
  for(int i=0; i < output_size; i++){ 
     base_place* p = self->p_output[i];
     int ori_size = p->tokens_len(); 
     self->po_w[i](p); 
     int new_size = p->tokens_len();
     for(int i=ori_size; i<new_size; i++){
       p->set_token_ts(i, self->delay_event);
     }
  }
}

int delay(transition* self){
  return self->delay_f();
}


int trigger(transition* self){

  if(self->delay_event != -1) return 1;
  if(self->disable) return 0;
  int enabled = 0;
  int can_fire = able_to_fire_t(self, enabled);
  
  if(self->delay_event == -1 && can_fire){
     int delay_time = delay(self);
     int enable_time = max(enabled, self->pip_ts);
     int mature_time = enable_time + delay_time; 
     if (self->pip != -1) {
        self->pip_ts = enable_time+self->pip;
     }else{
        self->pip_ts = mature_time;
     }
     self->delay_event = mature_time;
    //  self->count += 1;
  }
  return can_fire;
}

int min_time(transition* self){

   if(self->delay_event != -1) 
     return self->delay_event;
   return LARGE;
}

int min_time_g(transition** all_ts, int size){

  int min = LARGE;
   for(int i=0; i<size; i++){
    int _t = min_time(all_ts[i]);
    // printf("%d ", _t);
    if (min > _t)
        min = _t;
  }
  return min;
}

vector<transition*>* min_time_t(transition** all_ts, int min_t, int size){
  vector<transition*>* min_ts = new vector<transition*>;
   for(int i=0; i<size; i++){
    int _t = min_time(all_ts[i]);
    // printf("%d ", _t);
    if (min_t == _t){
        min_ts->push_back(all_ts[i]);
    }
  }
  return min_ts;
}


int sync(transition* self, int time){

   if (self->delay_event == -1){
      // self->time = time;
      return 1;
   }
   
   if(time >= self->delay_event){
    // reordered the two
     accept_t(self);
     fire_t(self);
     self->delay_event = -1;
   }
   return 0;
}


// int sync_independent(transition* self){

//    if (self->delay_event == -1){
//       return 1;
//    }
   
//    if(time >= self->delay_event){
//     // reordered the two
//      accept_t(self);
//      fire_t(self);
//      self->delay_event = -1;
//    }
//    return 0;
// }

int trigger_for_path(transition* self){

  return trigger(self);
  int enabled_ts = 0;
  int can_fire = able_to_fire_t(self, enabled_ts);
  return can_fire;

}

int sync_for_path(transition* self){
  
  self->count += 1;
  // cout << self->id << endl;
  accept_t(self);
  fire_t(self);
  self->delay_event = -1;
  
  return 0;
}

void detect_conflicting_transition_groups(transition** t_list, int size, std::set<base_place*>& p_list, int* conflict_free){
  map<base_place*, vector<int>> potential_conflict;
  for(auto& p : p_list){
    for(int i=0; i<size; i++){
      for(auto& outp : t_list[i]->p_output){
        if(p == outp){
          potential_conflict[p].push_back(i);
          break;
        }
      }
    }
  }
  for(auto& pair: potential_conflict){
    std::vector<int>& transitions = pair.second;
    if(transitions.size() > 1){
      for(auto& ti : transitions){
        conflict_free[ti] = 0;
      }
    }
  }

}

#endif
