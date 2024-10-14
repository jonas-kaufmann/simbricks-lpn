#ifndef __LPN_REQ_MAP_HH
#define __LPN_REQ_MAP_HH

#include <assert.h>
#include <bits/stdint-uintn.h>

#include <condition_variable>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <deque>
#include <vector>

// TODO Rename MATCH Interface

typedef struct MemReq {
  uint32_t id;
  int tag;
  uint64_t addr;
  uint32_t len;
  uint32_t acquired_len;
  bool rw;
  void* buffer;
  uint64_t issued_ts;
  uint64_t complete_ts;
  // 0 is not ready for issue
  // 1 is ready for issue
  // 2 is issued
  // 3 is completes
  int issue = 0;

  ~MemReq() {
    free(buffer);
  }

} MemReq;

#define READ_REQ 0
#define WRITE_REQ 1

typedef struct DramReq : MemReq {
} DramReq;

extern std::map<int, std::deque<std::unique_ptr<MemReq>>> io_req_map;

void setupReqQueues(const std::vector<int>& ids);
void ClearReqQueues(const std::vector<int>& ids);

std::unique_ptr<MemReq>& frontReq(std::deque<std::unique_ptr<MemReq>>& reqQueue); 

std::unique_ptr<MemReq>& findReq(std::deque<std::unique_ptr<MemReq>>& reqQueue, uint64_t addr);

std::unique_ptr<MemReq>& enqueueReq(int id, uint64_t addr, uint32_t len, int tag, int rw);

std::unique_ptr<MemReq> dequeueReq(std::deque<std::unique_ptr<MemReq>>& reqQueue);

class Matcher {

 private:
  int tag;
  bool valid = false;

  public:
  std::vector<std::unique_ptr<MemReq>> reqs;
  std::unique_ptr<MemReq> currReq;
  Matcher() = default;
  explicit Matcher(int _tag) : tag(_tag) {}
  
  void Clear() {
    currReq.reset();
    reqs.clear();
    valid = false;
  }

  // Registers a request to be matched
  void Register(std::unique_ptr<MemReq> req) {
    // std::cerr << "Registering request" << " tag:" << req->tag << " rw:" << req->rw  << std::endl;
    assert(req->rw == READ_REQ);
    currReq = std::move(req);
    valid = true;
    MatchAll();
  }

  // Matches or Enqueues a buffered request
  void Produce(std::unique_ptr<MemReq> req) {
      reqs.emplace_back(std::move(req));
  }

  // Consumes the request, need to register again afterwards
  std::unique_ptr<MemReq> Consume() {
    std::unique_ptr<MemReq> req = std::move(currReq);
    valid = false;
    return req;
  }

  bool isCompleted() {
    return valid && (currReq->acquired_len == currReq->len);
  }

  bool isValid() {
    return valid;
  }

  // Matches buffered memory requests in current one
  void MatchAll() {
    
    if(isCompleted() || !valid){
      return;
    }

    auto start = currReq->addr;
    auto end = start + currReq->len;
    auto it = reqs.begin();
    // there will be only one request in the queue
    while (it != reqs.end()) {
      auto req = it->get();
      assert(start >= req->addr);
      if(req->addr + req->acquired_len >= end){
        // Copy memory
        auto from = start;
        auto to = end;
        auto offset2 = from - req->addr;
        memcpy((void*)((uint64_t)currReq->buffer), (void*)((uint64_t)req->buffer + offset2), to - from);
        currReq->acquired_len += to - from;
        // do you need to erase the request?
        // erase returns the next iterator
        if (currReq->acquired_len == currReq->len) {
          // std::cerr << "Matching request done" << " addr:" << currReq->addr << " acc_len:" << currReq->acquired_len <<  " len: " << currReq->len << std::endl;
          break;
        }
      }
      break;
    }
  }

};


class CtlVar {
 public:
  std::condition_variable cv;
  std::mutex mx;
  bool blocked = false;
  bool finished = false;
  bool exited = false;
  std::map<int, Matcher> req_matcher;
  
  void Reset() {
    std::unique_lock<std::mutex> lk(mx);
    for(auto& kv : req_matcher){
      kv.second.Clear();
    }
    blocked = false;
    finished = false;
    exited = false;
  }
  // explicit CtlVar(std::map<int, std::deque<std::unique_ptr<MemReq>>>& _req_map) : req_matcher(_req_map) {}
};

// can block if the data is not ready
void getData(CtlVar& ctrl, uint64_t addr, uint32_t len, int tag, int rw);
int getDataNB(CtlVar& ctrl, uint64_t addr, uint32_t len, int tag, int rw);
void putData(uint64_t addr, uint32_t len, int tag, int rw, uint64_t ts, void* buffer);


extern CtlVar ctl_func;
extern CtlVar ctl_iogen;
extern CtlVar ctl_nb_lpn;

extern int num_instr;

#endif
