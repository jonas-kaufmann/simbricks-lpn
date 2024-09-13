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
    if (!valid || !MatchReq(req)) {
      reqs.emplace_back(std::move(req));
    }
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

 private:
  // Matches buffered memory requests in current one
  void MatchAll() {
    auto start = currReq->addr;
    auto end = start + currReq->len;
    int tag = currReq->tag;
    auto it = reqs.begin();
    while (it != reqs.end()) {
      auto req = it->get();
      // Check bounds
      if(req->acquired_len != req->len){
        ++it;
        continue;
      }
      if (req->addr + req->len > start && req->addr < end) {
        // Copy memory
        auto from = std::max(start, req->addr);
        auto to = std::min(end, req->addr + req->len);
        auto offset1 = from - start;
        auto offset2 = from - req->addr;
        memcpy((void*)((uint64_t)currReq->buffer + offset1), (void*)((uint64_t)req->buffer + offset2), to - from);
        currReq->acquired_len += to - from;
        // std::cerr << "Matching request" << " tag:" << tag << " addr:" << req->addr << " acc_len:" << currReq->acquired_len <<  " len: " << currReq->len << std::endl;


        if (to - from < req->len) {
          std::cerr <<"type :" << tag << " Start: " << start << " End: " << end << std::endl;
          std::cerr << "Checking bounds: " << req->addr << " " << req->len << std::endl;
          assert(0);  // If overlapping requests
          // Create new request with the correct info
          // Add it to vector
        }

        // do you need to erase the request?
        // erase returns the next iterator
        it = reqs.erase(it);
        if (currReq->acquired_len == currReq->len) {
          break;
        }
        continue;
      }
      ++it;
    }
  }

  // Matches a single request
  bool MatchReq(std::unique_ptr<MemReq>& req) {
    // Else try to match the request
    auto start = currReq->addr;
    auto end = start + currReq->len;

    if (req->addr + req->len > start && req->addr < end) {
      // Copy memory
      auto from = std::max(start, req->addr);
      auto to = std::min(end, req->addr + req->len);
      auto offset1 = from - start;
      auto offset2 = from - req->addr;
      memcpy((void*)((uint64_t)currReq->buffer + offset1), (void*)((uint64_t)req->buffer + offset2), to - from);
      currReq->acquired_len += to - from;

      if (to - from < req->len) {
        std::cerr << "tag:" << tag << " :" << currReq->tag <<" Checking bounds: " << req->addr << " " << req->len << " curr: " << currReq->addr << " len: " <<  currReq->len << std::endl;
        assert(0);  // If overlapping requests
        // Create new request with the correct info
        // Add it to vector
      }
      return true;
    }
    return false;
  }
};


class CtlVar {
 public:
  std::condition_variable cv;
  std::mutex mx;
  bool blocked = false;
  bool finished = false;
  std::map<int, Matcher> req_matcher;
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
