#ifndef JPEG_BIT_BUFFER_H
#define JPEG_BIT_BUFFER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "rollback_buf.hh"
#define ddprintf

#ifndef TEST_HOOKS_BITBUFFER
#define TEST_HOOKS_BITBUFFER(x)
#endif

#ifndef TEST_HOOKS_BITBUFFER_DECL
#define TEST_HOOKS_BITBUFFER_DECL
#endif

//-----------------------------------------------------------------------------
// jpeg_bit_buffer:
//-----------------------------------------------------------------------------
class jpeg_bit_buffer
{
public:
    
    size_t global_buf_idx = 0;
    size_t global_buf_len = 0;
    uint8_t* global_buf = NULL;
    uint8_t global_m_last = 0;
    size_t global_m_rd_offset = 0;
    int last_detect_marker = 0;
    int last_is_padding = 0;
    int marker_detected = 0;


    jpeg_bit_buffer() 
    {
        m_buffer = NULL;
        reset(-1);
    }

    void reset(int max_size = -1)
    {
        if (m_buffer)
        {
            delete [] m_buffer;
            m_buffer = NULL;
        }

        if (max_size <= 0)
            m_max_size = 1 << 20;
        else
            m_max_size = max_size;

        m_buffer = new uint8_t[m_max_size];
        memset(m_buffer, 0, m_max_size);
        m_wr_offset = 0;
        m_last      = global_m_last;
        m_rd_offset = global_m_rd_offset;
        dprintf("reset m_rd_offset to %d \n", m_rd_offset);
        dprintf("at reset last_detect %d \n", last_detect_marker);
        global_m_last = 0;
        global_m_rd_offset = 0;
    }

    // Push byte into stream (return false if marker found)
    bool push(uint8_t b)
    {
        uint8_t last = m_last;
        last_is_padding=0;
        // Skip padding
        if (last == 0xFF && b == 0x00 && m_wr_offset >= (last_detect_marker-global_buf_idx)){
            last_is_padding=1;
        }
        // Marker found
        else if (last == 0xFF && b != 0x00 && m_wr_offset >= (last_detect_marker-global_buf_idx))
        {
            m_wr_offset--;
            marker_detected = 1;
            dprintf("didn't push byte %0x \n", b);
            return false;
        }
        // Push byte into buffer
        else
        {
            // assert();
            assert(m_wr_offset < m_max_size ? 1 : (dprintf("Assertion failed: %d %d\n", m_wr_offset, m_max_size), 0));
            dprintf("push byte %0x \n", b);
            m_buffer[m_wr_offset++] = b;
        }

        m_last = b;
        return true;
    }
    void copy_out(uint8_t* buf){
        for(int i = 0; i<m_wr_offset; i++){
            buf[global_buf_idx + i] = m_buffer[i];
        }
        global_buf_len = global_buf_idx + m_wr_offset;
        last_detect_marker = global_buf_len + last_is_padding;
    }
    // Read upto 32-bit (aligned to MSB)
    uint32_t read_word(void)
    {
        if (eof()){
            printf("EOF \n");
            return 0;
        }
        dprintf("m_rd_offset %d", m_rd_offset%8);
        dprintf("read with offset %d\n", m_rd_offset);
        int byte   = m_rd_offset / 8;
        int bit    = m_rd_offset % 8; // 0 - 7
        uint64_t w = 0;
        for (int x=0;x<5;x++)
        {
            w |= m_buffer[byte+x];
            dprintf(" == byte %0x global buf %0x gbufidx %d \n ", m_buffer[byte+x], global_buf[byte+x], global_buf_idx);
            w <<= 8;
        }
        w <<= bit;
        return w >> 16;
    }

    void advance(int bits)
    {
        TEST_HOOKS_BITBUFFER(bits);
        m_rd_offset += bits;
    }

    bool eof(void)
    {
        // dprintf("check eof %d, %d\n", m_rd_offset, m_wr_offset);
        // skip eof, return 0; even though it's not enough
        // if(yes) return 1;
        bool ans = 0;
        if(marker_detected){
            int yes = CheckNotEnoughBuf(global_buf_idx+(m_rd_offset+7)/8, global_buf_len+1, global_buf);
            ans = (((m_rd_offset+7) / 8) >= m_wr_offset);
        }
        else{
            int yes = CheckNotEnoughBuf(global_buf_idx+(m_rd_offset)/8, global_buf_len, global_buf);
            ans = ((m_rd_offset / 8) >= m_wr_offset);
        }
        
        return ans;
    }
    
    TEST_HOOKS_BITBUFFER_DECL;

// private:
    uint8_t *m_buffer;
    uint8_t  m_last = 0;
    int      m_max_size = 0;
    int      m_wr_offset = 0;
    int      m_rd_offset = 0;  // in bits
};

#endif
