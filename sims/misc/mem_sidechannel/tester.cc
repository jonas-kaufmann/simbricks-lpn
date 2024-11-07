#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ios>
#include <iostream>
#include <limits>
#include <thread>

#include <simbricks/base/cxxatomicfix.h>
extern "C" {
#include <simbricks/base/if.h>
#include <simbricks/mem/if.h>
#include <simbricks/mem/proto.h>
}

static SimbricksMemIf mem_if;

// NOTE: We are actually the host-side of the memory protocol since we are
// sending the requests
static bool simbricks_connect(const char* const socket_path) {
  SimbricksProtoMemHostIntro host_intro;
  SimbricksProtoMemMemIntro mem_intro;
  SimbricksBaseIfParams params;

  if (!socket_path) {
    std::cerr << __func__ << " socket path not set but required" << std::endl;
    return false;
  }

  SimbricksMemIfDefaultParams(&params);
  params.blocking_conn = true;
  params.sock_path = socket_path;
  params.sync_mode = kSimbricksBaseIfSyncDisabled;

  if (SimbricksBaseIfInit(&mem_if.base, &params)) {
    std::cerr << __func__ << " SimbricksBaseIfInit failed" << std::endl;
    return false;
  }

  if (SimbricksBaseIfConnect(&mem_if.base)) {
    std::cerr << __func__ << " SimbricksBaseIfConnect failed" << std::endl;
    return false;
  }

  if (SimbricksBaseIfConnected(&mem_if.base)) {
    std::cerr << __func__ << " SimbricksBaseIfConnected indicates unconnected"
              << std::endl;
    return false;
  }

  // Prepare & send host intro
  std::memset(&host_intro, 0, sizeof(host_intro));
  if (SimbricksBaseIfIntroSend(&mem_if.base, &host_intro, sizeof(host_intro))) {
    std::cerr << __func__ << " SimbricksBaseIfIntroSend failed" << std::endl;
    return false;
  }

  // Receive device intro
  size_t len = sizeof(mem_intro);
  if (SimbricksBaseIfIntroRecv(&mem_if.base, &mem_intro, &len)) {
    std::cerr << __func__ << " SimbricksBaseIfIntroRecv failed" << std::endl;
    return false;
  }
  if (len != sizeof(mem_intro)) {
    std::cerr << __func__ << " rx dev intro: length is not as expected"
              << std::endl;
    return false;
  }

  return true;
}

static uint64_t read_mem(uint64_t addr) {
  // Send read request
  volatile union SimbricksProtoMemH2M* msg =
      SimbricksMemIfH2MOutAlloc(&mem_if, 0);
  volatile SimbricksProtoMemH2MRead& read_msg = msg->read;

  if (!msg) {
    std::cout << __func__ << " SimbricksMemIfH2MOutAlloc() failed" << std::endl;
    throw;
  }

  read_msg.addr = addr;
  read_msg.len = sizeof(uint64_t);
  SimbricksMemIfH2MOutSend(&mem_if, msg, SIMBRICKS_PROTO_MEM_H2M_MSG_READ);

  // Poll for incoming message
  volatile union SimbricksProtoMemM2H* in_msg;
  while (true) {
    in_msg =
        SimbricksMemIfM2HInPoll(&mem_if, std::numeric_limits<uint64_t>::max());
    if (in_msg) {
      break;
    }
    std::this_thread::yield();
  }

  uint8_t msg_type = SimbricksMemIfM2HInType(&mem_if, in_msg);
  if (msg_type != SIMBRICKS_PROTO_MEM_M2H_MSG_READCOMP) {
    std::cerr << __func__ << " unexpected msg type " << msg_type << ""
              << std::endl;
    throw;
  }

  volatile SimbricksProtoMemM2HReadcomp& read_comp = in_msg->readcomp;
  uint64_t data;
  std::memcpy(&data, const_cast<uint8_t*>(read_comp.data), sizeof(data));
  SimbricksMemIfM2HInDone(&mem_if, in_msg);
  return data;
}

static void write_mem(uint64_t addr, uint64_t data) {
  // Send write request
  volatile union SimbricksProtoMemH2M* msg =
      SimbricksMemIfH2MOutAlloc(&mem_if, 0);
  volatile SimbricksProtoMemH2MWrite& write_msg = msg->write;

  if (!msg) {
    std::cout << __func__ << " SimbricksMemIfH2MOutAlloc() failed" << std::endl;
    throw;
  }

  write_msg.addr = addr;
  write_msg.len = sizeof(data);
  std::memcpy(const_cast<uint8_t*>(write_msg.data), &data, sizeof(data));

  SimbricksMemIfH2MOutSend(&mem_if, msg,
                           SIMBRICKS_PROTO_MEM_H2M_MSG_WRITE_POSTED);
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: tester <simbricks_socket>" << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "connecting to SimBricks" << std::endl;

  if (!simbricks_connect(argv[1])) {
    std::cerr << "simbricks_connect() failed" << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "SimBricks connected, starting test..." << std::endl;

  // Wait for host-side update of value
  uint64_t dma_region_base = 1024 * 1024 * 1024;
  uint64_t read_val = 0;
  do {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    read_val = read_mem(dma_region_base);
    std::cout << "read val 0x" << std::hex << read_val << " at addr 0x"
              << dma_region_base << "" << std::endl;
  } while (read_val != 0x1234567890123456);
  std::cout << "waiting for value test successful" << std::endl;

  // Aligned write read
  uint64_t write_data = 0x1122334455667788;
  uint64_t write_addr = dma_region_base + 8;
  write_mem(write_addr, write_data);
  uint64_t read_data = read_mem(write_addr);
  if (read_data == write_data) {
    std::cout << "aligned write and read test successful" << std::endl;
  } else {
    std::cerr << "aligned write and read test failed write_data=0x" << std::hex
              << write_data << " read_data=0x" << read_data << std::endl;
    return EXIT_FAILURE;
  }

  // Unaligned write read spanning two cachelines
  write_data = 0x8877665544332211;
  write_addr = dma_region_base + 62;
  write_mem(write_addr, write_data);
  read_data = read_mem(write_addr);
  if (read_data == write_data) {
    std::cout << "unaligned write and read test successful" << std::endl;
  } else {
    std::cerr << "unaligned write and read test failed write_data=0x" << std::hex
              << write_data << " read_data=0x" << read_data << std::endl;
    return EXIT_FAILURE;
  }


  return EXIT_SUCCESS;
}