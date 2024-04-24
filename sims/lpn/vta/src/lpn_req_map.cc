#include "sims/lpn/vta/include/lpn_req_map.hh"

std::map<int, std::deque<std::unique_ptr<LpnReq>>> lpn_req_map;
std::map<int, Matcher> func_req_map;
std::map<int, Matcher> perf_req_map;

std::condition_variable cv;
std::mutex m_proc;
bool sim_blocked;
bool finished;

int num_instr = 0;

void setupReqQueues(const std::vector<int>& ids) {
  for (const auto& id : ids) {
    lpn_req_map[id] = std::deque<std::unique_ptr<LpnReq>>();
    func_req_map[id] = Matcher();
    perf_req_map[id] = Matcher();
  }
}

