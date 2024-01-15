#ifndef __LPN_HELPER_ROLLBACK_BUF__
#define __LPN_HELPER_ROLLBACK_BUF__
#include <stdlib.h>
#include <stdint.h>

static size_t last_idx;
static size_t last_buf_size = 0; 

uint8_t* GetGlobalBuffer() {
    static uint8_t* buffer = (uint8_t*)malloc(sizeof(uint8_t)*4096*2);
    return buffer;
}

uint8_t* AugmentBufWithLast(uint8_t* buf, size_t& len){
    uint8_t* last_buf = GetGlobalBuffer();
    if(last_buf_size == 0) return buf;
    for(int i = 0; i < len; i++){
        last_buf[i+last_buf_size] = buf[i];
    }
    len = len+last_buf_size;
    return last_buf;
}

int CheckNotEnoughBuf(int future_idx, size_t len, uint8_t* buf){
    uint8_t* last_buf = GetGlobalBuffer();
    //future_idx is accessed
    if(future_idx >= len){
        for(int i = last_idx; i < len; i++){
            last_buf[i-last_idx] = buf[i];
        }
        last_buf_size = len - last_idx;
        return 1; 
    }else{
        return 0;
    }
}

void CheckPointIdx(int cur) {
    last_idx = cur;
}

#define CHECK_ENOUGH_BUF(future, len, buf, ret)\
    if(CheckNotEnoughBuf(future, len, buf)){ \
        return ret; \
    }


#endif