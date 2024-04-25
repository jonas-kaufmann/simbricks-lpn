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
  std::unique_ptr<MemReq> currReq;  
  std::vector<std::unique_ptr<MemReq>> reqs;
  bool valid = false;

 public:
  // Registers a request to be matched
  void Register(std::unique_ptr<MemReq> req) {
    currReq = std::move(req);
    valid = true;
    Match();
  }

  // Enqueues a buffered request
  void Produce(std::unique_ptr<MemReq> req) {
    reqs.emplace_back(std::move(req));
  }

  // Matches buffered memory requests in current one
  void Match() {
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
          assert(0);  // If overlapping requests
          // Create new request with the correct info
          // Add it to vector
        }
        it = reqs.erase(it);
        continue;
      }
      ++it;
    }
  }

  bool isCompleted() {
    return (currReq->acquired_len == currReq->len);
  }

  bool isValid() {
    return valid;
  }

  // Consumes the request, need to register again afterwards
  std::unique_ptr<MemReq> Consume() {
    std::unique_ptr<MemReq> req = std::move(currReq);
    valid = false;
    return req;
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
