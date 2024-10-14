#include <bits/stdint-uintn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <cstddef>
#include <iostream>
#include <vector>
#include <cmath>
#include "c_model/common.h"
#include "c_model/jpeg_dqt.h"
#include "c_model/jpeg_dht.h"
#include "c_model/jpeg_idct.h"
#include "c_model/jpeg_idct_ifast.h"
#include "c_model/jpeg_bit_buffer.h"
#include "c_model/jpeg_mcu_block.h"
#include "sims/lpn/jpeg_decoder/lpn_def/places.hh"
#include "sims/lpn/jpeg_decoder/include/driver.hh"
#include "sims/lpn/jpeg_decoder/include/lpn_req_map.hh"

#define EXTRA_BYTES 6*64*4

static uint64_t timestamp = 0;
static int finished = 0;
static jpeg_dqt        m_dqt;
static jpeg_dht        m_dht;
#if defined(IDCT_IFAST)
static jpeg_idct_ifast m_idct;
#else
static jpeg_idct       m_idct;
#endif
static jpeg_bit_buffer m_bit_buffer;
static jpeg_mcu_block  m_mcu_dec(&m_bit_buffer, &m_dht);

// static std::vector<int> mcu_cnt;

static uint16_t m_width = 0;
static uint16_t m_height = 0;
static size_t rgb_consumed_len = 0;
static int num_tokens_for_cur_img = 0;

static int block_num = 0;
static int loop = 0;

static uint8_t* m_output_r = nullptr;
static uint8_t* m_output_g = nullptr;
static uint8_t* m_output_b = nullptr;

static uint8_t last_b = 0;
static uint8_t b = 0;
static bool decode_done = false;
static int state = 0;
static int mcu_start = 0;

bool IsCurImgFinished() {
    // not used
   return false;
}

void Reset() {
    std::cerr << "Calling Reset to the whole LPN\n";
    m_width = 0;
    m_height = 0;
    rgb_consumed_len = 0;
    num_tokens_for_cur_img = 0;

    last_b = 0;
    b = 0;
    decode_done = false;
    state = 0;
    mcu_start = 0;

    block_num = 0;
    loop = 0;

    timestamp = 0;
    finished = 0;
    m_dqt.reset();
    m_dht.reset();
    m_idct.reset();
    m_bit_buffer.reset();
    m_mcu_dec.reset();

    free(m_output_r);
    free(m_output_g);
    free(m_output_b);

    m_output_r = nullptr;
    m_output_g = nullptr;
    m_output_b = nullptr;

    ptasks.reset();
    pdone.reset();
}

using t_jpeg_mode = enum eJpgMode
{
    JPEG_MONOCHROME,
    JPEG_YCBCR_444,
    JPEG_YCBCR_420,
    JPEG_UNSUPPORTED
};

static t_jpeg_mode m_mode = JPEG_UNSUPPORTED;

static uint8_t m_dqt_table[3];

#define get_byte(var, _buf, _idx)  var = (_buf)[(_idx)++]
#define get_byte_no_assign(_buf, _idx)  (_idx)++
               
#define get_word(var, _buf, _idx) var = ((_buf[_idx] << 8) | (_buf[_idx+1])); _idx+=2
#define get_word_no_assign(_buf, _idx)  _idx+=2

// #define ddprintf_blk(_name, _arr, _max) for (int __i=0;__i<_max;__i++) { ddprintf("%s: %d -> %d\n", _name, __i, _arr[__i]); }
#define ddprintf_blk(...) 

size_t GetSizeOfRGB(){
    return int(std::ceil(m_height/8.0) * std::ceil(m_width/8.0)*64);
}

size_t GetCurRGBOffset(){
    std::cout << "Get cur RGB offset " << pdone.tokensLen()*64 << std::endl;
    int lpn_size = pdone.tokensLen()*64*4; 
    if(lpn_size == 0){
        return 0;
    }
    int rgb_size =  GetSizeOfRGB();
    return std::min(lpn_size, rgb_size);
}

size_t GetConsumedRGBOffset(){
    std::cout << "Get consumed RGB offset " << rgb_consumed_len << std::endl;
    return rgb_consumed_len;
}

void UpdateConsumedRGBOffset(size_t len){
    std::cout << "Update consumed RGB offset " << len << std::endl;
    rgb_consumed_len = len;
}

uint8_t *GetMOutputR(){
    if(m_output_r==nullptr){
        m_output_r = new uint8_t[GetSizeOfRGB()];
        memset(m_output_r, 0, GetSizeOfRGB());
    }
    return m_output_r;
}

uint8_t *GetMOutputG(){
    if(m_output_g==nullptr){
        m_output_g = new uint8_t[GetSizeOfRGB()];
        memset(m_output_g, 0, GetSizeOfRGB());
    }
    return m_output_g;
}

uint8_t *GetMOutputB(){
    if(m_output_b==nullptr){
        m_output_b = new uint8_t[GetSizeOfRGB()];
        memset(m_output_b, 0, GetSizeOfRGB());
    }
    return m_output_b;
}

//-----------------------------------------------------------------------------
// ConvertYUV2RGB: Convert from YUV to RGB
//-----------------------------------------------------------------------------
static void ConvertYUV2RGB(int block_num, int *y, int *cb, int *cr)
{
    uint8_t* m_output_r = GetMOutputR();
    uint8_t* m_output_g = GetMOutputG();
    uint8_t* m_output_b = GetMOutputB();

    int x_blocks = (m_width / 8);

    // If width is not a multiple of 8, round up
    if (m_width % 8)
        x_blocks++;

    int x_start = (block_num % x_blocks) * 8;
    int y_start = (block_num / x_blocks) * 8;

    if (m_mode == JPEG_MONOCHROME)
    {
        for (int i=0;i<64;i++)
        {
            int r = 128 + y[i];
            int g = 128 + y[i];
            int b = 128 + y[i];

            // Avoid overflows
            r = (r & 0xffffff00) ? (r >> 24) ^ 0xff : r;
            g = (g & 0xffffff00) ? (g >> 24) ^ 0xff : g;
            b = (b & 0xffffff00) ? (b >> 24) ^ 0xff : b;

            int _x = x_start + (i % 8);
            int _y = y_start + (i / 8);
            int offset = (_y * m_width) + _x;
            // rgb_cur_len = offset+1;
            // rgb_cur_len += 1;
            dprintf("RGB: r=%d g=%d b=%d -> %d\n", r, g, b, offset);
           
            m_output_r[offset] = r;
            m_output_g[offset] = g;
            m_output_b[offset] = b;
        }
    }
    else
    {
        for (int i=0;i<64;i++)
        {
            int r = 128 + y[i] + (cr[i] * 1.402);
            int g = 128 + y[i] - (cb[i] * 0.34414) - (cr[i] * 0.71414);
            int b = 128 + y[i] + (cb[i] * 1.772);

            // Avoid overflows
            r = (r & 0xffffff00) ? (r >> 24) ^ 0xff : r;
            g = (g & 0xffffff00) ? (g >> 24) ^ 0xff : g;
            b = (b & 0xffffff00) ? (b >> 24) ^ 0xff : b;

            int _x = x_start + (i % 8);
            int _y = y_start + (i / 8);
            int offset = (_y * m_width) + _x;
            // rgb_cur_len = offset+1;
            // rgb_cur_len += 1;

            if (_x < m_width && _y < m_height)
            {
                dprintf("RGB: r=%d g=%d b=%d -> %d [x=%d,y=%d]\n", r, g, b, offset, _x, _y);
                m_output_r[offset] = r;
                m_output_g[offset] = g;
                m_output_b[offset] = b;
            }
        }
    }
}

//-----------------------------------------------------------------------------
// DecodeImage: Decode image data section (supports 4:4:4, 4:2:0, monochrom)
//-----------------------------------------------------------------------------
static bool DecodeImage(int till_end)
{
    int16_t dc_coeff_Y = 0;
    int16_t dc_coeff_Cb= 0;
    int16_t dc_coeff_Cr= 0;
    int32_t sample_out[64];
    int     block_out[64];
    int     y_dct_out[4*64];
    int     cb_dct_out[64];
    int     cr_dct_out[64];
    int     count = 0;
    int     loop = 0;

    int count_6[6] = {0};
    while (!m_bit_buffer.eof())
    {
        // [Y0 Y1 Y2 Y3 Cb Cr] x N
        if (m_mode == JPEG_YCBCR_420)
        {
            ddprintf("start decoding \n");
            // lets do 6 blocks at a time between unroll.
            // Y0
            count = m_mcu_dec.decode(DHT_TABLE_Y_DC_IDX, dc_coeff_Y, sample_out);
            if (count == -1) return false;
            count_6[0]= count;
            m_dqt.process_samples(m_dqt_table[0], sample_out, block_out, count);
            ddprintf_blk("DCT-IN", block_out, 64);
            m_idct.process(block_out, &y_dct_out[0]);

            // Y1
            count = m_mcu_dec.decode(DHT_TABLE_Y_DC_IDX, dc_coeff_Y, sample_out);
            if (count == -1) return false;
            count_6[1]= count;
            m_dqt.process_samples(m_dqt_table[0], sample_out, block_out, count);
            ddprintf_blk("DCT-IN", block_out, 64);
            m_idct.process(block_out, &y_dct_out[64]);

            // Y2
            count = m_mcu_dec.decode(DHT_TABLE_Y_DC_IDX, dc_coeff_Y, sample_out);
            if (count == -1) return false;
            count_6[2]= count;
            m_dqt.process_samples(m_dqt_table[0], sample_out, block_out, count);
            ddprintf_blk("DCT-IN", block_out, 64);
            m_idct.process(block_out, &y_dct_out[128]);

            // Y3
            count = m_mcu_dec.decode(DHT_TABLE_Y_DC_IDX, dc_coeff_Y, sample_out);
            if (count == -1) return false;
            count_6[3]= count;            
            m_dqt.process_samples(m_dqt_table[0], sample_out, block_out, count);
            ddprintf_blk("DCT-IN", block_out, 64);
            m_idct.process(block_out, &y_dct_out[192]);

            // Cb
            count = m_mcu_dec.decode(DHT_TABLE_CX_DC_IDX, dc_coeff_Cb, sample_out);
            if (count == -1) return false;
            count_6[4]= count;
            m_dqt.process_samples(m_dqt_table[1], sample_out, block_out, count);
            ddprintf_blk("DCT-IN", block_out, 64);
            m_idct.process(block_out, &cb_dct_out[0]);

            // Cr
            count = m_mcu_dec.decode(DHT_TABLE_CX_DC_IDX, dc_coeff_Cr, sample_out);
            if (count == -1) return false;
            count_6[5]= count;
            m_dqt.process_samples(m_dqt_table[2], sample_out, block_out, count);
            ddprintf_blk("DCT-IN", block_out, 64);
            m_idct.process(block_out, &cr_dct_out[0]);

            // Expand Cb/Cr samples to match Y0-3
            int cb_dct_out_x2[256];
            int cr_dct_out_x2[256];

            for (int i=0;i<64;i++)
            {
                int x = i % 8;
                int y = i / 16;
                int sub_idx = (y * 8) + (x / 2);
                cb_dct_out_x2[i] = cb_dct_out[sub_idx];
                cr_dct_out_x2[i] = cr_dct_out[sub_idx];
            }

            for (int i=0;i<64;i++)
            {
                int x = i % 8;
                int y = i / 16;
                int sub_idx = (y * 8) + 4 + (x / 2);
                cb_dct_out_x2[64 + i] = cb_dct_out[sub_idx];
                cr_dct_out_x2[64 + i] = cr_dct_out[sub_idx];
            }

            for (int i=0;i<64;i++)
            {
                int x = i % 8;
                int y = i / 16;
                int sub_idx = 32 + (y * 8) + (x / 2);
                cb_dct_out_x2[128+i] = cb_dct_out[sub_idx];
                cr_dct_out_x2[128+i] = cr_dct_out[sub_idx];
            }

            for (int i=0;i<64;i++)
            {
                int x = i % 8;
                int y = i / 16;
                int sub_idx = 32 + (y * 8) + 4 + (x / 2);
                cb_dct_out_x2[192 + i] = cb_dct_out[sub_idx];
                cr_dct_out_x2[192 + i] = cr_dct_out[sub_idx];
            }

            int mcu_width = m_width / 8;
            if (m_width % 8)
                mcu_width++;

            // Output all 4 blocks of pixels
            ConvertYUV2RGB((block_num/2) + 0, &y_dct_out[0],  &cb_dct_out_x2[0], &cr_dct_out_x2[0]);
            ConvertYUV2RGB((block_num/2) + 1, &y_dct_out[64], &cb_dct_out_x2[64], &cr_dct_out_x2[64]);
            ConvertYUV2RGB((block_num/2) + mcu_width + 0, &y_dct_out[128], &cb_dct_out_x2[128], &cr_dct_out_x2[128]);
            ConvertYUV2RGB((block_num/2) + mcu_width + 1, &y_dct_out[192], &cb_dct_out_x2[192], &cr_dct_out_x2[192]);
            block_num += 4;

            if (++loop == (mcu_width / 2))
            {
                block_num += (mcu_width * 2);
                loop = 0;
            }
        }
        // [Y Cb Cr] x N
        // else if (m_mode == JPEG_YCBCR_444)
        // {
        //     // Y
        //     count = m_mcu_dec.decode(DHT_TABLE_Y_DC_IDX, dc_coeff_Y, sample_out);
        //     m_dqt.process_samples(m_dqt_table[0], sample_out, block_out, count);
        //     ddprintf_blk("DCT-IN", block_out, 64);
        //     m_idct.process(block_out, &y_dct_out[0]);

        //     // Cb
        //     count = m_mcu_dec.decode(DHT_TABLE_CX_DC_IDX, dc_coeff_Cb, sample_out);
        //     m_dqt.process_samples(m_dqt_table[1], sample_out, block_out, count);
        //     ddprintf_blk("DCT-IN", block_out, 64);
        //     m_idct.process(block_out, &cb_dct_out[0]);

        //     // Cr
        //     count = m_mcu_dec.decode(DHT_TABLE_CX_DC_IDX, dc_coeff_Cr, sample_out);
        //     m_dqt.process_samples(m_dqt_table[2], sample_out, block_out, count);
        //     ddprintf_blk("DCT-IN", block_out, 64);
        //     m_idct.process(block_out, &cr_dct_out[0]);

        //     ConvertYUV2RGB(block_num++, y_dct_out, cb_dct_out, cr_dct_out);
        // }
        // // [Y] x N
        // else if (m_mode == JPEG_MONOCHROME)
        // {
        //     // Y
        //     count = m_mcu_dec.decode(DHT_TABLE_Y_DC_IDX, dc_coeff_Y, sample_out);
        //     m_dqt.process_samples(m_dqt_table[0], sample_out, block_out, count);
        //     ddprintf_blk("DCT-IN", block_out, 64);
        //     m_idct.process(block_out, &y_dct_out[0]);

        //     ConvertYUV2RGB(block_num++, y_dct_out, cb_dct_out, cr_dct_out);
        // }
        ddprintf("finished 6 blocks \n");
        finished += 6;
        for(int i : count_6){
            ddprintf("cnt %d dc_Y %d \n", i, dc_coeff_Y);
        }
        printf("producing lpn tokens %lu\n", timestamp);
        for(int cnt : count_6){
            NEW_TOKEN(mcu_token, new_token);
            new_token->delay = 3*(cnt) + 6;
            new_token->ts = timestamp;
            pvarlatency.tokens.push_back(new_token);
            
            NEW_TOKEN(EmptyToken, ne_token);
            ne_token->ts = timestamp;
            ptasks.tokens.push_back(ne_token);
        }

        if(till_end == 0){
            break;
        }
        // if(finished > 203*6) assert(0);
    }
    return true;
}

void writeout_img();

#include "sims/lpn/jpeg_decoder/include/lpn_req_map.hh"

#define GETDATA(offset, size) \
getData(ctl_func, (uint64_t)src_addr+offset, size, 0, READ_REQ); \
auto front = ctl_func.req_matcher[0].Consume(); \
memcpy(&buf[offset], front->buffer, front->len); 

int jpeg_decode_funcsim(uint64_t src_addr, size_t src_len, uint64_t dst_addr, uint64_t ts)
{

    std::cerr << "jpeg decoder funcsim: src_addr=" << src_addr << " dst_addr=" << dst_addr << "\n";
    timestamp = ts;
    uint8_t *buf = (uint8_t *)calloc(1, src_len+EXTRA_BYTES);
    int len = src_len;
    #define DMA_BLOCK_SIZE 32
    ddprintf("update lpn state with bytes of length %d\n", len);
    for (int i=0;i<len;)
    {
        std::cout << "driver want some data, index i at " << i << " addr:" << src_addr+i << "\n";
        GETDATA(i, DMA_BLOCK_SIZE);
        std::cout << "After get some data, index i at " << i << "\n";
        
        // i always points to next unaccessed slots
        if (! mcu_start){
            b = buf[i++];
            ddprintf("b 0x%0x, last_b 0x%0x \n", b, last_b);
        }
        
        //-----------------------------------------------------------------------------
        // SOI: Start of image
        //-----------------------------------------------------------------------------
        if (last_b == 0xFF && b == 0xd8){
            ddprintf("Section: SOI\n");
        }
        //-----------------------------------------------------------------------------
        // SOF0: Indicates that this is a baseline DCT-based JPEG
        //-----------------------------------------------------------------------------
        else if ((last_b == 0xFF && b == 0xc0))
        {
            // CheckPointIdx(i);
            ddprintf("Section: SOF0\n");
            int seg_start = i;
            
            // Length of the segment

            // get_word increases i to i+2
            uint16_t seg_len;
            get_word(seg_len, buf, i);

            // Precision of the frame data
            uint8_t  precision; 
            get_byte(precision, buf, i);

            // Image height in pixels
            get_word(m_height, buf, i);

            // Image width in pixels
            get_word(m_width, buf, i);
            
            // num_tokens_for_cur_img = int(std::ceil(m_width/8.0)*std::ceil(m_height/8.0));
            // this calculation is buggy
            num_tokens_for_cur_img = std::ceil(m_width/8.0)*std::ceil(m_height/8.0);
            std::cout << "num_tokens_for_cur_img " << num_tokens_for_cur_img << std::endl;

            GetMOutputR();
            GetMOutputG();
            GetMOutputB();

            // # of components (n) in frame, 1 for monochrom, 3 for colour images
            uint8_t num_comps;
            get_byte(num_comps, buf, i);
            assert(num_comps <= 3);

            ddprintf(" x=%d, y=%d, components=%d\n", m_width, m_height, num_comps);
            uint8_t comp_id[3];
            uint8_t comp_sample_factor[3];
            uint8_t horiz_factor[3];
            uint8_t vert_factor[3];

            for (int x=0;x<num_comps;x++)
            {
                // First byte identifies the component
                get_byte(comp_id[x], buf,i);
                // id: 1 = Y, 2 = Cb, 3 = Cr

                // Second byte represents sampling factor (first four MSBs represent horizonal, last four LSBs represent vertical)
                get_byte(comp_sample_factor[x], buf, i);
                horiz_factor[x]       = comp_sample_factor[x] >> 4;
                vert_factor[x]        = comp_sample_factor[x] & 0xF;
                // Third byte represents which quantization table to use for this component
                get_byte_no_assign(buf,i);
            }

            m_mode = JPEG_UNSUPPORTED;

            // Single component (Y)
            if (num_comps == 1)
            {
                ddprintf(" Mode: Monochrome\n");
                m_mode = JPEG_MONOCHROME;
            }
            // Colour image (YCbCr)
            else if (num_comps == 3)
            {
                // YCbCr ordering expected
                if (comp_id[0] == 1 && comp_id[1] == 2 && comp_id[2] == 3)
                {
                    if (horiz_factor[0] == 1 && vert_factor[0] == 1 &&
                        horiz_factor[1] == 1 && vert_factor[1] == 1 &&
                        horiz_factor[2] == 1 && vert_factor[2] == 1)
                    {
                        m_mode = JPEG_YCBCR_444;
                        ddprintf(" Mode: YCbCr 4:4:4\n");
                    }
                    else if (horiz_factor[0] == 2 && vert_factor[0] == 2 &&
                             horiz_factor[1] == 1 && vert_factor[1] == 1 &&
                             horiz_factor[2] == 1 && vert_factor[2] == 1)
                    {
                        m_mode = JPEG_YCBCR_420;
                        ddprintf(" Mode: YCbCr 4:2:0\n");
                    }
                }
            }

            i = seg_start + seg_len;
        }
        //-----------------------------------------------------------------------------
        // DQT: Quantisation table
        //-----------------------------------------------------------------------------
        else if (last_b == 0xFF && b == 0xdb)
        {
            //CheckPointIdx(i);
            ddprintf("Section: DQT Table\n");
            int seg_start = i;

            uint16_t seg_len;
            get_word(seg_len, buf, i);

            GETDATA(i, seg_len);

            // CHECK_ENOUGH_BUF(seg_start+seg_len-1, len, buf, 0); 
            m_dqt.process(&buf[i], seg_len);
            i = seg_start + seg_len;
        }
        //-----------------------------------------------------------------------------
        // DHT: Huffman table
        //-----------------------------------------------------------------------------
        else if (last_b == 0xFF && b == 0xc4)
        {
            //CheckPointIdx(i);
            int seg_start = i;
            

            uint16_t seg_len; 
            get_word(seg_len, buf, i);

            ddprintf("Section: DHT Table\n");

            GETDATA(i, seg_len);
            // CHECK_ENOUGH_BUF(seg_start+seg_len-1, len, buf, 0);
            m_dht.process(&buf[i], seg_len);
            i = seg_start + seg_len;
        }
        //-----------------------------------------------------------------------------
        // EOI: End of image
        //-----------------------------------------------------------------------------
        else if (last_b == 0xFF && b == 0xd9)
        {
            ddprintf("Section: EOI\n");
            break;
        }
        //-----------------------------------------------------------------------------
        // SOS: Start of Scan Segment (SOS)
        //-----------------------------------------------------------------------------
        else if (last_b == 0xFF && b == 0xda)
        {
            ddprintf("Section: SOS\n");
            //CheckPointIdx(i);
            int seg_start = i;

            if (m_mode == JPEG_UNSUPPORTED)
            {
                ddprintf("ERROR: Unsupported JPEG mode\n");
                break;
            }

            uint16_t seg_len;
            get_word(seg_len, buf, i);

            // Component count (n)
            uint8_t  comp_count; 
            get_byte(comp_count, buf,i);

            // Component data
            for (int x=0;x<comp_count;x++)
            {
                // First byte denotes component ID
                uint8_t comp_id;
                get_byte(comp_id, buf, i);

                // Second byte denotes the Huffman table used (first four MSBs denote Huffman table for DC, and last four LSBs denote Huffman table for AC)
                uint8_t comp_table;
                get_byte(comp_table, buf,i);

                ddprintf(" %d: ID=%x Table=%x\n", x, comp_id, comp_table);
            }

            // Skip bytes
            get_byte_no_assign(buf,i);
            get_byte_no_assign(buf,i);
            get_byte_no_assign(buf,i);

            i = seg_start + seg_len;
            // CHECK_ENOUGH_BUF(seg_start + seg_len - 1, len, buf, 0);
            
            //-----------------------------------------------------------------------
            // Process data segment
            //-----------------------------------------------------------------------
            #define BLOCK6BYTES 6*64*4
            m_bit_buffer.reset(len+BLOCK6BYTES);

            while(i < len){
                int j = 0;
                int marker_detected = 0;
                std::cout << "need to get 6 blocks of data :" << BLOCK6BYTES << " addr: " << i+src_addr << "\n";
                GETDATA(i, BLOCK6BYTES);
                std::cout << "get 6 blocks of data done" << "\n";
                while(j < BLOCK6BYTES){
                    b = buf[i+j];
                    if (m_bit_buffer.push(b))
                        j++;
                    // Marker detected (reverse one byte)
                    else
                    {
                        j--;
                        std::cout << "marker detected\n";
                        marker_detected = 1;
                        break;
                    }
                }

                if(marker_detected){
                    // decode till the end
                    i += j;
                    decode_done = DecodeImage(1);
                    std::cout << "break out\n";
                    break;
                }
                i += j;
                // decode one 6 blocks
                std::cout << "decode 6 blocks\n";
                decode_done = DecodeImage(0); 
                std::cout << "decode 6 blocks finish\n";
            }
            // while (i < len)
            // {
            //     b = buf[i];
            //     if (m_bit_buffer.push(b))
            //         i++;
            //     // Marker detected (reverse one byte)
            //     else
            //     {
            //         i--;
            //         break;
            //     }
            // }
            // ddprintf("decode img\n");
            // decode_done = DecodeImage();
        }
         else if (last_b == 0xFF && b == 0xc2)
        {
            ddprintf("Section: SOF2\n");
            int seg_start = i;
            uint16_t seg_len;
            get_word(seg_len, buf, i);
            i = seg_start + seg_len;

            ddprintf("ERROR: Progressive JPEG not supported\n");
            break; // ERROR: Not supported
        }
        else if (last_b == 0xFF && b == 0xdd)
        {
            ddprintf("Section: DRI\n");
            int seg_start = i;
            uint16_t seg_len;
            get_word(seg_len, buf, i);
            i = seg_start + seg_len;            
        }
        else if (last_b == 0xFF && b >= 0xd0 && b <= 0xd7)
        {
            ddprintf("Section: RST%d\n", b - 0xd0);
            int seg_start = i;
            uint16_t seg_len;
            get_word(seg_len, buf, i);
            i = seg_start + seg_len;
        }
        else if (last_b == 0xFF && b >= 0xe0 && b <= 0xef)
        {
            ddprintf("Section: APP%d\n", b - 0xe0);
            int seg_start = i;
            uint16_t seg_len;
            get_word(seg_len, buf, i);
            i = seg_start + seg_len;
        }
        else if (last_b == 0xFF && b == 0xfe)
        {
            ddprintf("Section: COM\n");
            int seg_start = i;
            uint16_t seg_len;
            get_word(seg_len, buf, i);
            i = seg_start + seg_len;
        }

        last_b = b;
    }
    

    {
      std::unique_lock<std::mutex> lk(ctl_func.mx);
      std::cout << "Funcsim Set finish to True!" << std::endl;
      ctl_func.finished = true; 
      ctl_func.blocked = true;
      ctl_func.cv.notify_one();
      ctl_func.cv.wait(lk, []{return !ctl_func.blocked;});
    }

    free(buf);
    std::cout << "Funcsim Exits" << std::endl;



    // int sum = 0;
    // // ddprintf("done with mcu_cnt size %d \n", mcu_cnt.size());
    // for (size_t i = 0; i < mcu_cnt.size(); ++i) {
    //     sum += 3*(mcu_cnt[i])+6;
    //     NEW_TOKEN(mcu_token, new_token);
    //     new_token->delay = 3*(mcu_cnt[i]) + 6;
    //     pvarlatency.tokens.push_back(new_token);
    // }
    // // ddprintf("avrage eob %f , sum eob %d \n", (double)sum/mcu_cnt.size(), sum);
    // create_empty_queue(&(ptasks.tokens), mcu_cnt.size());
    // if (m_output_r) delete [] m_output_r;
    // if (m_output_g) delete [] m_output_g;
    // if (m_output_b) delete [] m_output_b;

    return 0;
}

void writeout_img(){
    const char *dst_image = "test.ppm";
    uint8_t* m_output_r = GetMOutputR();
    uint8_t* m_output_g = GetMOutputG();
    uint8_t* m_output_b = GetMOutputB();
    {
        FILE *f = fopen(dst_image, "w");
        if (f)
        {
            fprintf(f, "P6\n");
            fprintf(f, "%d %d\n", m_width, m_height);
            fprintf(f, "255\n");
            for (int y=0;y<m_height;y++)
                for (int x=0;x<m_width;x++)
                {
                    putc(m_output_r[(y*m_width)+x], f);
                    putc(m_output_g[(y*m_width)+x], f);
                    putc(m_output_b[(y*m_width)+x], f);
                }
            fclose(f);
        }
        else
        {
            fprintf(stderr, "ERROR: Could not write file\n");
        }
    }

}