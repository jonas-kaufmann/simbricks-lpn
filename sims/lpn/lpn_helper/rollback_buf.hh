#ifndef __LPN_HELPER_ROLLBACK_BUF__
#define __LPN_HELPER_ROLLBACK_BUF__
#include <stdlib.h>
#include <stdint.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#define dprintf
#define BUF_SIZE 8192 * 2

static size_t last_idx = 0;
static size_t last_buf_size = 0; 
static uint8_t* buffer = nullptr;
static uint8_t* new_buffer = nullptr;

uint8_t* GetGlobalBuffer() {
    if(buffer==nullptr){
        buffer = static_cast<uint8_t*>(malloc(sizeof(uint8_t)*BUF_SIZE));
    }
    return buffer;
}

uint8_t* GetNewBuffer() {
    if(new_buffer==nullptr){
        new_buffer = static_cast<uint8_t*>(malloc(sizeof(uint8_t)*BUF_SIZE));
        memset(new_buffer, 0, sizeof(uint8_t) * BUF_SIZE);
    }
    return new_buffer;
}
uint8_t* AugmentBufWithLast(uint8_t* buf, size_t& len){
    uint8_t* last_buf = GetGlobalBuffer();
    if(last_buf_size == 0) return buf;
    assert(len + last_buf_size < sizeof(uint8_t) * BUF_SIZE && "AugmentBufWithLast() new data doesn't fit in buffer");
    dprintf(" alloc new buf with size %zu \n", len);
    uint8_t* new_buf = GetNewBuffer();
    for(int i = 0; i < last_buf_size; i++){
        new_buf[i] = last_buf[i];
    }
    for(int i = 0; i < len; i++){
        new_buf[i+last_buf_size] = buf[i];
    }
    len = len+last_buf_size;
    dprintf("AugmentBufWithLast last_buf_size %zu, new len %zu \n", last_buf_size, len);
    // reset
    last_buf_size = 0;
    last_idx = 0;
    return new_buf;
}

int CheckNotEnoughBuf(size_t future_idx, size_t len, const uint8_t* buf){
    //future_idx is accessed
    if(future_idx >= len){
        uint8_t* last_buf = GetGlobalBuffer();
        memset(last_buf, 0, sizeof(uint8_t)*8192*2);
        for(size_t i = last_idx; i < len; i++){
            last_buf[i-last_idx] = buf[i];
        }
        last_buf_size = len - last_idx;
        dprintf("Not Enough, len %zu, checked-idx %zu \n", len, last_idx);
        
        return 1; 
    } 
    return 0;
   
}

void CheckPointIdx(int cur) {
    last_idx = cur;
    dprintf("checkidx %zu \n", last_idx);
}

// void CheckPointIdx(int cur, size_t len, uint8_t* buf) {
//     last_idx = cur;
//     uint8_t* last_buf = GetGlobalBuffer();
//     for(int i = last_idx; i < len; i++){
//         last_buf[i-last_idx] = buf[i];
//     }
//     last_buf_size = len - last_idx;
//     dprintf("Checkpointing, len %d, checked-idx %d \n", len, last_idx);
// }

void RollLog(){
    dprintf("last_idx %d, last_buf_size %d \n", last_idx, last_buf_size);
}
#define CHECK_ENOUGH_BUF(future, len, buf, ret)\
    if(CheckNotEnoughBuf(future, len, buf)){ \
        dprintf(#future "<" #len "\n"); \
        return ret; \
    }


void RollbackBufReset(){
    last_buf_size = 0;
    last_idx = 0;
    free(buffer);
    free(new_buffer);
    buffer = nullptr;
    new_buffer = nullptr;
}

#endif
