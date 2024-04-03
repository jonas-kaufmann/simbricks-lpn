#include "sims/lpn/vta/include/lpn_req_map.hh"
#include <mutex>
#include <condition_variable>

std::map<int, std::queue<std::unique_ptr<LpnReq>>> lpn_req_map;
std::map<int, std::queue<std::unique_ptr<DramReq>>> dram_req_map;
std::map<int, std::unique_ptr<FixedDoubleBuffer>> read_buffer_map;
std::map<int, std::unique_ptr<FixedDoubleBuffer>> write_buffer_map;

std::mutex m_proc;
bool sim_blocked;

bool sim_req;
bool wrap_req;

std::condition_variable cv;
bool sim_run;

std::condition_variable cv_req;
std::condition_variable cv_resp;
std::mutex m_req;
std::mutex m_resp;
bool wait_req;
bool wait_resp;
bool finished;


void setupReqQueues(const std::vector<int>& ids) {
    for (const auto& id : ids) {
        lpn_req_map[id] = std::queue<std::unique_ptr<LpnReq>>();
        dram_req_map[id] = std::queue<std::unique_ptr<DramReq>>();
    }
}

void setupBufferMap(const std::vector<int>& ids) {
    for (int id : ids) {
        read_buffer_map[id] = std::make_unique<FixedDoubleBuffer>(1024*300);
        write_buffer_map[id] = std::make_unique<FixedDoubleBuffer>(1024*300);
    }
}
