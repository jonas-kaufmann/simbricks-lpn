#include "sims/lpn/jpeg_decoder/include/lpn_req_map.hh"

std::map<int, std::deque<std::unique_ptr<MemReq>>> io_req_map;
CtlVar ctl_func;
CtlVar ctl_iogen;
CtlVar ctl_nb_lpn;
int num_instr;

void setupReqQueues(const std::vector<int>& ids) {
  for (const auto& id : ids) {
    // io_req_map[id] = std::deque<std::unique_ptr<MemReq>>();
    ctl_func.req_matcher[id] = Matcher(id);
  }
}


void ClearReqQueues(const std::vector<int>& ids) {
  for (const auto& id : ids) {
    // io_req_map[id].clear();
    ctl_func.req_matcher[id].Clear();
    // ctl_iogen.req_matcher[id].Clear();
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
  ctl_func.req_matcher[tag].Produce(std::move(req));
  return ctl_func.req_matcher[tag].reqs.back();
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

  // Wait for Response
  {
    std::unique_lock<std::mutex> lk(ctrl.mx);
    while (!matcher.isCompleted()) {
      ctrl.blocked = true;
      ctrl.cv.notify_one();
      ctrl.cv.wait(lk, [&] { return !ctrl.blocked; });
    }
    // std::cout  << "getData completed ? : " << matcher.isCompleted() << std::endl;
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
  std::vector<std::unique_ptr<MemReq>>& reqs = ctl_func.req_matcher[tag].reqs;
  auto it = reqs.begin();
  while (it != reqs.end()) {
    auto req = it->get();
    // Check bounds
    // assume req is larger than writes
    if(req->acquired_len == req->len){
      break;
    }

    if(addr >= req->addr && addr + len <= req->addr + req->len){
      // Copy memory
      memcpy(req->buffer+addr-req->addr, buffer, len);
      req->acquired_len += len;
      ctl_func.req_matcher[tag].MatchAll();
      // std::cout << "putData" << " tag:" << tag << " addr:" << req->addr << " total_acquired_len:" << req->acquired_len << " new-len:" << len << std::endl;
      break;
    }
    ++it;
  }
}

