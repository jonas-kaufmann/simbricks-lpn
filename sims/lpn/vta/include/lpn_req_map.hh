#ifndef __LPN_REQ_MAP_HH
#define __LPN_REQ_MAP_HH

#include <bits/stdint-uintn.h>
#include <cstring>
#include <memory>
#include <vector>
#include <map>
#include <queue>

typedef struct LpnReq{
    uint32_t id;
    uint32_t len;
    uint32_t acquired_len; 
} LpnReq;

#define READ_REQ 0
#define WRITE_REQ 1

typedef struct DramReq{
    uint32_t id;
    uint64_t addr;
    uint32_t len;
    uint32_t acquired_len;
    bool started;
    bool rw;
} DramReq;

extern std::map<int, std::queue<std::unique_ptr<LpnReq>>> lpn_req_map;
extern std::map<int, std::queue<std::unique_ptr<DramReq>>> dram_req_map;

void setupReqQueues(const std::vector<int>& ids);

template<typename T>
std::unique_ptr<T>& frontReq(std::queue<std::unique_ptr<T>>& reqQueue) {
    return reqQueue.front();
}

template<typename T>
void enqueueReq(std::queue<std::unique_ptr<T>>& reqQueue, std::unique_ptr<T>& req) {
    reqQueue.push(std::move(req));
}

template<typename T>
std::unique_ptr<T> dequeueReq(std::queue<std::unique_ptr<T>>& reqQueue) {
    std::unique_ptr<T> req = std::move(reqQueue.front());
    reqQueue.pop();
    return req;
}

class FixedDoubleBuffer {
private:
    uint8_t* buffers_[2]; // Array of two buffers
    int currentBuffer_; // Index of the buffer currently in use for writing
    size_t bufferSize_; // Size of each buffer
    size_t readPtr_; // Read pointer in the current buffer
    size_t dataLen_; // Amount of valid data in the current buffer

public:
    FixedDoubleBuffer(size_t size)
        : currentBuffer_(0), bufferSize_(size), readPtr_(0), dataLen_(0) {
        buffers_[0] = new uint8_t[size];
        buffers_[1] = new uint8_t[size];
        memset(buffers_[0], 0, size);
        memset(buffers_[1], 0, size);
    }

    ~FixedDoubleBuffer() {
        delete[] buffers_[0];
        delete[] buffers_[1];
    }

    void supply(uint8_t * data, size_t len) {
        size_t spaceAvailable = bufferSize_ - dataLen_ - readPtr_;
        if (len <= spaceAvailable) {
            // If new data fits in the current buffer, copy it there
            memcpy(buffers_[currentBuffer_] + readPtr_ + dataLen_, data, len);
            dataLen_ += len;
        } else {
            // Not enough space, so switch buffers
            int newBuffer = 1 - currentBuffer_;
            memcpy(buffers_[newBuffer], buffers_[currentBuffer_] + readPtr_, dataLen_); // Copy existing valid data to new buffer
            memcpy(buffers_[newBuffer] + dataLen_, data, len); // Append new data
            currentBuffer_ = newBuffer;
            readPtr_ = 0; // Reset read pointer
            dataLen_ += len; // Update valid data length
        }
    }

    uint8_t* getHead() const {
        return buffers_[currentBuffer_] + readPtr_;
    }

    void pop(size_t len) {
        // Adjust read pointer and data length without moving data
        size_t popLength = std::min(len, dataLen_);
        readPtr_ += popLength;
        dataLen_ -= popLength;
        
        // If all data is consumed, reset readPtr for potential reuse of this buffer space
        if (dataLen_ == 0) {
            readPtr_ = 0;
        }
    }

    size_t getLength() const {
        return dataLen_;
    }

    void setLength(size_t len) {
        dataLen_ = len;
    }
};

extern std::map<int, std::unique_ptr<FixedDoubleBuffer>> read_buffer_map;
extern std::map<int, std::unique_ptr<FixedDoubleBuffer>> write_buffer_map;

void setupBufferMap(const std::vector<int>& ids);

#endif
