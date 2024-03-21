#include "sims/lpn/vta/include/lpn_req_map.hh"

std::map<int, std::queue<std::unique_ptr<LpnReq>>> lpn_req_map;
std::map<int, std::queue<std::unique_ptr<DramReq>>> dram_req_map;
std::map<int, std::unique_ptr<FixedDoubleBuffer>> read_buffer_map;
std::map<int, std::unique_ptr<FixedDoubleBuffer>> write_buffer_map;

void setupReqQueues(const std::vector<int>& ids) {
    for (const auto& id : ids) {
        lpn_req_map[id] = std::queue<std::unique_ptr<LpnReq>>();
        dram_req_map[id] = std::queue<std::unique_ptr<DramReq>>();
    }
}

void setupBufferMap(const std::vector<int>& ids) {
    for (int id : ids) {
        read_buffer_map[id] = std::make_unique<FixedDoubleBuffer>(10240);
        write_buffer_map[id] = std::make_unique<FixedDoubleBuffer>(10240);
    }
}
