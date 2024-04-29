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
#include "sims/lpn/vta/include/vta/driver.h"

// TODO Rename MATCH Interface

typedef struct MemReq {
  uint32_t id;
  uint64_t addr;
  uint32_t len;
  uint32_t acquired_len;
  bool rw;
  void* buffer;

  ~MemReq() {
    free(buffer);
  }
} MemReq;

// TODO find better name like PerfWrite/PerfRead, FuncWrite/FuncRead
typedef struct LpnReq : MemReq {
  bool inflight; 
} LpnReq;

#define READ_REQ 0
#define WRITE_REQ 1

typedef struct DramReq : MemReq {
} DramReq;

extern std::map<int, std::deque<std::unique_ptr<LpnReq>>> lpn_req_map;

void setupReqQueues(const std::vector<int>& ids);
void ClearReqQueues(const std::vector<int>& ids);

template <typename T>
std::unique_ptr<T>& frontReq(std::deque<std::unique_ptr<T>>& reqQueue) {
  return reqQueue.front();
}

template <typename T>
void enqueueReq(std::deque<std::unique_ptr<T>>& reqQueue,
                std::unique_ptr<T> req) {
  reqQueue.push_back(std::move(req));
}

template <typename T>
std::unique_ptr<T> dequeueReq(std::deque<std::unique_ptr<T>>& reqQueue) {
  std::unique_ptr<T> req = std::move(reqQueue.front());
  reqQueue.pop_front();
  return req;
}

class Matcher {
 private:
  int tag;
  std::unique_ptr<MemReq> currReq;  
  std::vector<std::unique_ptr<MemReq>> reqs;
  bool valid = false;

   public:
    // Default constructor
    Matcher() = default;
    explicit Matcher(int id) : tag(id) {}
   
    void Clear() {
      currReq.reset();
      reqs.clear();
      valid = false;
    }

  // Registers a request to be matched
  void Register(std::unique_ptr<MemReq> req) {
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
    // if(valid && tag == 0)
      // std::cerr <<"Matcher tag:" << tag <<  " acquired_len: " << currReq->acquired_len << " len: " << currReq->len << std::endl;
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

    auto it = reqs.begin();
    while (it != reqs.end()) {
      auto req = it->get();
      // Check bounds
      if (req->addr + req->len > start && req->addr < end) {
        // Copy memory
        auto from = std::max(start, req->addr);
        auto to = std::min(end, req->addr + req->len);
        auto offset1 = from - start;
        auto offset2 = from - req->addr;
        memcpy(currReq->buffer + offset1, req->buffer + offset2, to - from);
        currReq->acquired_len += to - from;

        if (to - from < req->len) {
          std::cerr <<"type :" << tag << " Start: " << start << " End: " << end << std::endl;
          std::cerr << "Checking bounds: " << req->addr << " " << req->len << std::endl;
          assert(0);  // If overlapping requests
          // Create new request with the correct info
          // Add it to vector
        }
        
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
      memcpy(currReq->buffer + offset1, req->buffer + offset2, to - from);
      currReq->acquired_len += to - from;

      if (to - from < req->len) {
        assert(0);  // If overlapping requests
        // Create new request with the correct info
        // Add it to vector
      }
      return true;
    }
    return false;
  }
};

Matcher& enqRequest(uint64_t addr, uint32_t len, int tag, int rw);

extern std::condition_variable cv;
extern std::mutex m_proc;
extern bool sim_blocked;
extern bool finished;

extern int num_instr;

extern std::map<int, Matcher> func_req_map;
extern std::map<int, Matcher> perf_req_map;

#endif
