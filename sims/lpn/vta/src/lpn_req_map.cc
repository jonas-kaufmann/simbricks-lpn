#include "sims/lpn/vta/include/lpn_req_map.hh"
#include "sims/lpn/vta/include/vta/driver.h"

std::map<int, std::deque<std::unique_ptr<MemReq>>> io_req_map;
CtlVar ctl_func;
CtlVar ctl_iogen;
CtlVar ctl_nb_lpn;
int num_instr;

void setupReqQueues(const std::vector<int>& ids) {
  for (const auto& id : ids) {
    io_req_map[id] = std::deque<std::unique_ptr<MemReq>>();
    ctl_func.req_matcher[id] = Matcher(id);
    ctl_iogen.req_matcher[id] = Matcher(id);
  }
}


void ClearReqQueues(const std::vector<int>& ids) {
  for (const auto& id : ids) {
    io_req_map[id].clear();
    ctl_func.req_matcher[id].Clear();
    ctl_iogen.req_matcher[id].Clear();
  }
}

std::unique_ptr<MemReq>& frontReq(std::deque<std::unique_ptr<MemReq>>& reqQueue){
  return reqQueue.front();
}


std::unique_ptr<MemReq>& enqueueReq(int id, uint64_t addr, uint32_t len, int tag, int rw) {
  auto req = std::make_unique<MemReq>();
  req->addr = addr;
  req->tag = tag;
  req->id = id;
  req->rw = rw;
  req->len = len;
  req->buffer = calloc(1, len);
  // Register Request to be Matched
  auto& reqQueue = io_req_map[tag];
  reqQueue.push_back(std::move(req));
  return reqQueue.back();
}


std::unique_ptr<MemReq> dequeueReq(std::deque<std::unique_ptr<MemReq>>& reqQueue) {
  std::unique_ptr<MemReq> req = std::move(reqQueue.front());
  reqQueue.pop_front();
  return req;
}


// blocking version of getData
void getData(CtlVar& ctrl, uint64_t addr, uint32_t len, int tag, int rw) {

  auto req = std::make_unique<MemReq>();
  req->addr = addr;
  req->tag = tag;
  req->rw = READ_REQ;
  req->len = len;
  req->buffer = calloc(1, len);
  // Register Request to be Matched
  auto& matcher = ctrl.req_matcher[tag];
  matcher.Register(std::move(req));

  // std::cout << "getData completed ? : " << matcher.isCompleted() << std::endl;
  // Wait for Response
  {
    std::unique_lock<std::mutex> lk(ctrl.mx);
    while (!matcher.isCompleted()) {
      // std::cout  << "getData completed ? : " << matcher.isCompleted() << std::endl;
      ctrl.blocked = true;
      ctrl.cv.notify_one();
      ctrl.cv.wait(lk, [&] { return !ctrl.blocked; });
    }
  }
}


// non-blocking version
int getDataNB(CtlVar& ctrl, uint64_t addr, uint32_t len, int tag, int rw) {

  auto req = std::make_unique<MemReq>();
  req->addr = addr;
  req->tag = tag;
  req->rw = READ_REQ;
  req->len = len;
  req->buffer = calloc(1, len);
  // Register Request to be Matched
  auto& matcher = ctrl.req_matcher[tag];
  matcher.Register(std::move(req));
  if(matcher.isCompleted()){
    return 1;
  }

  matcher.Consume();
  return 0;
}


void putData(uint64_t addr, uint32_t len, int tag, int rw, uint64_t ts, void* buffer) {
  // std::cerr << "Matching write request" << " tag:" << writeReq->tag << "rw:" << writeReq->rw  << std::endl;
  std::deque<std::unique_ptr<MemReq>>& reqs = io_req_map[tag];
  auto it = reqs.begin();
  while (it != reqs.end()) {
    auto req = it->get();
    // Check bounds
    // assume req is larger than writes
    if(req->acquired_len == req->len){
      ++it;
      continue;
    }
    if(addr >= req->addr && addr + len <= req->addr + req->len){
      // Copy memory
      memcpy(req->buffer+addr-req->addr, buffer, len);
      req->acquired_len += len;
      if(req->acquired_len == req->len){
        // finished
        if(req->issue == 2){
          req->issue = 3;
          req->complete_ts = ts;
        }
        if(req->rw == READ_REQ){
          auto copy1 = std::make_unique<MemReq>(*req);
          copy1->buffer = calloc(1, req->len);
          memcpy(copy1->buffer, req->buffer, req->len);
          auto copy2 = std::make_unique<MemReq>(*req);
          copy2->buffer = calloc(1, req->len);
          memcpy(copy2->buffer, req->buffer, req->len);

          ctl_func.req_matcher[tag].Produce(std::move(copy1));
          ctl_iogen.req_matcher[tag].Produce(std::move(copy2));
          if(tag == LOAD_INSN){
            auto copy3 = std::make_unique<MemReq>(*req);
            copy3->buffer = calloc(1, req->len);
            memcpy(copy3->buffer, req->buffer, req->len);
            // std::cerr << "!!! Producing LPN request for tag: " << tag << std::endl;
            ctl_nb_lpn.req_matcher[tag].Produce(std::move(copy3));
          }          
        }
      }
      // std::cerr << "Matching write request" << " tag:" << tag << " addr:" << req->addr << " acc_len:" << req->acquired_len << " len:" << req->len << std::endl;
      break;
    }
    ++it;
  }
}

