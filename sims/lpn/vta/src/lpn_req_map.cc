#include "sims/lpn/vta/include/lpn_req_map.hh"

std::map<int, std::deque<std::unique_ptr<LpnReq>>> lpn_req_map;
std::map<int, Matcher> func_req_map;
std::map<int, Matcher> perf_req_map;

bool finished;

int num_instr = 0;

coroutine h;


void setupReqQueues(const std::vector<int>& ids) {
  for (const auto& id : ids) {
    lpn_req_map[id] = std::deque<std::unique_ptr<LpnReq>>();
    func_req_map[id] = Matcher();
    perf_req_map[id] = Matcher();
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
  return matcher;
}
