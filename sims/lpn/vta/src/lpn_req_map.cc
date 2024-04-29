#include "sims/lpn/vta/include/lpn_req_map.hh"

std::map<int, std::deque<std::unique_ptr<LpnReq>>> lpn_req_map;
std::map<int, Matcher> func_req_map;
std::map<int, Matcher> perf_req_map;

std::condition_variable cv;
std::mutex m_proc;
bool sim_blocked = false;
bool finished = false;

int num_instr = 0;

void setupReqQueues(const std::vector<int>& ids) {
  for (const auto& id : ids) {
    lpn_req_map[id] = std::deque<std::unique_ptr<LpnReq>>();
    func_req_map[id] = Matcher(id);
    perf_req_map[id] = Matcher(id);
  }
}


void ClearReqQueues(const std::vector<int>& ids) {
  for (const auto& id : ids) {
    lpn_req_map[id].clear();
    func_req_map[id].Clear();
    perf_req_map[id].Clear();
  }
}


Matcher& enqRequest(uint64_t addr, uint32_t len, int tag, int rw) {
  auto req = std::make_unique<DramReq>();
  req->addr = (uint64_t)addr;
  req->id = tag;
  req->rw = READ_REQ;
  req->len = len;
  req->buffer = calloc(1, len);
  // Register Request to be Matched
  auto& matcher = func_req_map[tag];
  matcher.Register(std::move(req));

  // Wait for Response
  {
    std::unique_lock lk(m_proc);
    while (!matcher.isCompleted()) {
      sim_blocked = true;
      cv.notify_one();
      cv.wait(lk, [] { return !sim_blocked; });
    }
  }
  return matcher;
}
