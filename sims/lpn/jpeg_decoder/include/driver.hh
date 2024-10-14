#pragma once
#include <stdint.h>
#include <string.h>
int jpeg_decode_funcsim(uint64_t src_addr, size_t src_len, uint64_t dst_addr, uint64_t ts);
size_t GetSizeOfRGB();
void Reset();
size_t GetCurRGBOffset();
size_t GetConsumedRGBOffset();
void UpdateConsumedRGBOffset(size_t len);
uint8_t *GetMOutputR();
uint8_t *GetMOutputG();
uint8_t *GetMOutputB();