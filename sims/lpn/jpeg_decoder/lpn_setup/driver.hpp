#ifndef __JPEG_DECODER_DRIVER__
#define __JPEG_DECODER_DRIVER__
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <vector>
#include <cmath>

#include "jpeg_dqt.h"
#include "jpeg_dht.h"
#include "jpeg_idct.h"
#include "jpeg_idct_ifast.h"
#include "jpeg_bit_buffer.h"
#include "jpeg_mcu_block.h"
#include "../../lpn_helper/rollback_buf.hh"
#include "../lpn_def/places.hh"

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

static std::vector<int> mcu_cnt;

static uint16_t m_width;
static uint16_t m_height;
static int num_tokens_for_cur_img = 0;

bool IsCurImgFinished() {
    if (num_tokens_for_cur_img == 0){
        return false;
    }
    return pdone.tokensLen() == int(std::ceil(m_width/8.0)*std::ceil(m_height/8.0));
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

#define get_byte(var, _buf, _idx)  CHECK_ENOUGH_BUF(_idx, len, _buf, 0); var = _buf[_idx++]
#define get_byte_no_assign(_buf, _idx)  CHECK_ENOUGH_BUF(_idx, len, _buf, 0); _buf[_idx++]
               
#define get_word(var, _buf, _idx)  CHECK_ENOUGH_BUF(_idx+1, len, _buf, 0); var = ((_buf[_idx++] << 8) | (_buf[_idx++]))
#define get_word_no_assign(_buf, _idx)  CHECK_ENOUGH_BUF(_idx+1, len, _buf, 0); ((_buf[_idx++] << 8) | (_buf[_idx++]))

#define dprintf
#define dprintf_blk(_name, _arr, _max) for (int __i=0;__i<_max;__i++) { dprintf("%s: %d -> %d\n", _name, __i, _arr[__i]); }

size_t GetSizeOfRGB(){
    return m_height * m_width;
}
uint8_t *GetMOutputR(){
    static uint8_t *m_output = nullptr;
    if(!m_output){
        m_output = new uint8_t[m_height * m_width];
        memset(m_output, 0, m_height * m_width);
    }
    return m_output;
}

uint8_t *GetMOutputG(){
    static uint8_t *m_output = nullptr;
    if(!m_output){
        m_output = new uint8_t[m_height * m_width];
        memset(m_output, 0, m_height * m_width);
    }
    return m_output;
}

uint8_t *GetMOutputB(){
    static uint8_t *m_output = nullptr;
    if(!m_output){
        m_output = new uint8_t[m_height * m_width];
        memset(m_output, 0, m_height * m_width);
    }
    return m_output;
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
static bool DecodeImage(void)
{
    static int16_t CH_dc_coeff_Y = 0;
    static int16_t CH_dc_coeff_Cb= 0;
    static int16_t CH_dc_coeff_Cr= 0;

    int16_t dc_coeff_Y = 0;
    int16_t dc_coeff_Cb= 0;
    int16_t dc_coeff_Cr= 0;
    
    int32_t sample_out[64];
    int     block_out[64];
    int     y_dct_out[4*64];
    int     cb_dct_out[64];
    int     cr_dct_out[64];
    int     count = 0;

    dc_coeff_Y = CH_dc_coeff_Y;
    dc_coeff_Cb = CH_dc_coeff_Cb;
    dc_coeff_Cr = CH_dc_coeff_Cr;

    static int block_num = 0;
    static int loop = 0;
    int count_6[6] = {0};
    while (!m_bit_buffer.eof())
    // while (1)
    {
        // [Y0 Y1 Y2 Y3 Cb Cr] x N
        if (m_mode == JPEG_YCBCR_420)
        {
            printf("start decoding \n");
            // lets do 6 blocks at a time between unroll.
            CheckPointIdx(m_bit_buffer.global_buf_idx + m_bit_buffer.m_rd_offset/8);
            printf("checkpoint buf value %0x \n", m_bit_buffer.global_buf[m_bit_buffer.global_buf_idx + m_bit_buffer.m_rd_offset/8]);
            dprintf("update detect marker %d %d %d %d \n", m_bit_buffer.last_detect_marker, m_bit_buffer.global_buf_len, m_bit_buffer.global_buf_idx, m_bit_buffer.m_rd_offset/8);
            m_bit_buffer.last_detect_marker = m_bit_buffer.global_buf_len+m_bit_buffer.last_is_padding - (m_bit_buffer.global_buf_idx+m_bit_buffer.m_rd_offset/8);
            m_bit_buffer.global_m_rd_offset = (m_bit_buffer.m_rd_offset)%8;
            if( m_bit_buffer.global_buf_idx + m_bit_buffer.m_rd_offset/8 - 1 < 0){
                m_bit_buffer.global_m_last = 0;
                assert(0);
            }else{
                m_bit_buffer.global_m_last = m_bit_buffer.global_buf[m_bit_buffer.global_buf_idx + m_bit_buffer.m_rd_offset/8-1];
                if (m_bit_buffer.global_m_last == 0xFF)
                    m_bit_buffer.global_m_last = 0;
                dprintf("m_last checked %0x \n",  m_bit_buffer.global_m_last);
            }
            
            CH_dc_coeff_Y = dc_coeff_Y;
            CH_dc_coeff_Cb = dc_coeff_Cb;
            CH_dc_coeff_Cr = dc_coeff_Cr;
            // Y0
            count = m_mcu_dec.decode(DHT_TABLE_Y_DC_IDX, dc_coeff_Y, sample_out);
            if (count == -1) return false;
            count_6[0]= count;
            m_dqt.process_samples(m_dqt_table[0], sample_out, block_out, count);
            dprintf_blk("DCT-IN", block_out, 64);
            m_idct.process(block_out, &y_dct_out[0]);

            // Y1
            count = m_mcu_dec.decode(DHT_TABLE_Y_DC_IDX, dc_coeff_Y, sample_out);
            if (count == -1) return false;
            count_6[1]= count;
            m_dqt.process_samples(m_dqt_table[0], sample_out, block_out, count);
            dprintf_blk("DCT-IN", block_out, 64);
            m_idct.process(block_out, &y_dct_out[64]);

            // Y2
            count = m_mcu_dec.decode(DHT_TABLE_Y_DC_IDX, dc_coeff_Y, sample_out);
            if (count == -1) return false;
            count_6[2]= count;
            m_dqt.process_samples(m_dqt_table[0], sample_out, block_out, count);
            dprintf_blk("DCT-IN", block_out, 64);
            m_idct.process(block_out, &y_dct_out[128]);

            // Y3
            count = m_mcu_dec.decode(DHT_TABLE_Y_DC_IDX, dc_coeff_Y, sample_out);
            if (count == -1) return false;
            count_6[3]= count;            
            m_dqt.process_samples(m_dqt_table[0], sample_out, block_out, count);
            dprintf_blk("DCT-IN", block_out, 64);
            m_idct.process(block_out, &y_dct_out[192]);

            // Cb
            count = m_mcu_dec.decode(DHT_TABLE_CX_DC_IDX, dc_coeff_Cb, sample_out);
            if (count == -1) return false;
            count_6[4]= count;
            m_dqt.process_samples(m_dqt_table[1], sample_out, block_out, count);
            dprintf_blk("DCT-IN", block_out, 64);
            m_idct.process(block_out, &cb_dct_out[0]);

            // Cr
            count = m_mcu_dec.decode(DHT_TABLE_CX_DC_IDX, dc_coeff_Cr, sample_out);
            if (count == -1) return false;
            count_6[5]= count;
            m_dqt.process_samples(m_dqt_table[2], sample_out, block_out, count);
            dprintf_blk("DCT-IN", block_out, 64);
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
        //     dprintf_blk("DCT-IN", block_out, 64);
        //     m_idct.process(block_out, &y_dct_out[0]);

        //     // Cb
        //     count = m_mcu_dec.decode(DHT_TABLE_CX_DC_IDX, dc_coeff_Cb, sample_out);
        //     m_dqt.process_samples(m_dqt_table[1], sample_out, block_out, count);
        //     dprintf_blk("DCT-IN", block_out, 64);
        //     m_idct.process(block_out, &cb_dct_out[0]);

        //     // Cr
        //     count = m_mcu_dec.decode(DHT_TABLE_CX_DC_IDX, dc_coeff_Cr, sample_out);
        //     m_dqt.process_samples(m_dqt_table[2], sample_out, block_out, count);
        //     dprintf_blk("DCT-IN", block_out, 64);
        //     m_idct.process(block_out, &cr_dct_out[0]);

        //     ConvertYUV2RGB(block_num++, y_dct_out, cb_dct_out, cr_dct_out);
        // }
        // // [Y] x N
        // else if (m_mode == JPEG_MONOCHROME)
        // {
        //     // Y
        //     count = m_mcu_dec.decode(DHT_TABLE_Y_DC_IDX, dc_coeff_Y, sample_out);
        //     m_dqt.process_samples(m_dqt_table[0], sample_out, block_out, count);
        //     dprintf_blk("DCT-IN", block_out, 64);
        //     m_idct.process(block_out, &y_dct_out[0]);

        //     ConvertYUV2RGB(block_num++, y_dct_out, cb_dct_out, cr_dct_out);
        // }
        printf("finished 6 blocks \n");
        finished += 6;
        for(int i : count_6){
            printf("cnt %d dc_Y %d \n", i, dc_coeff_Y);
        }
        for(int cnt : count_6){
            NEW_TOKEN(mcu_token, new_token);
            new_token->delay = 3*(cnt) + 6;
            new_token->ts = timestamp;
            pvarlatency.tokens.push_back(new_token);
            
            NEW_TOKEN(EmptyToken, ne_token);
            ne_token->ts = timestamp;
            ptasks.tokens.push_back(ne_token);
        }
        // if(finished > 203*6) assert(0);
    }
    return true;
}

static uint8_t last_b = 0;
static uint8_t b = 0;
static bool decode_done = false;
static int state = 0;
static int mcu_start = 0;
void writeout_img();

int UpdateLpnState(uint8_t *buf, size_t len, uint64_t ts)
{
    timestamp = ts;
    buf = AugmentBufWithLast(buf, len);

    dprintf("update lpn state with bytes of length %d\n", len);
    for (int i=0;i<len;)
    {
        // i always points to next unaccessed slots
        if (! mcu_start){
            CheckPointIdx(i);
            CHECK_ENOUGH_BUF(i, len, buf, 0);
            b = buf[i++];
            dprintf("b 0x%0x, last_b 0x%0x \n", b, last_b);
        }
        
        //-----------------------------------------------------------------------------
        // SOI: Start of image
        //-----------------------------------------------------------------------------
        if (last_b == 0xFF && b == 0xd8);
            //printf("Section: SOI\n");
        //-----------------------------------------------------------------------------
        // SOF0: Indicates that this is a baseline DCT-based JPEG
        //-----------------------------------------------------------------------------
        else if ((last_b == 0xFF && b == 0xc0))
        {
            // CheckPointIdx(i);
            printf("Section: SOF0\n");
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

            GetMOutputR();
            GetMOutputG();
            GetMOutputB();

            // # of components (n) in frame, 1 for monochrom, 3 for colour images
            uint8_t num_comps;
            get_byte(num_comps, buf, i);
            assert(num_comps <= 3);

            printf(" x=%d, y=%d, components=%d\n", m_width, m_height, num_comps);
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
                printf(" Mode: Monochrome\n");
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
                        printf(" Mode: YCbCr 4:4:4\n");
                    }
                    else if (horiz_factor[0] == 2 && vert_factor[0] == 2 &&
                             horiz_factor[1] == 1 && vert_factor[1] == 1 &&
                             horiz_factor[2] == 1 && vert_factor[2] == 1)
                    {
                        m_mode = JPEG_YCBCR_420;
                        printf(" Mode: YCbCr 4:2:0\n");
                    }
                }
            }

            i = seg_start + seg_len;
            CHECK_ENOUGH_BUF(seg_start+seg_len-1, len, buf, 0);    
        }
        //-----------------------------------------------------------------------------
        // DQT: Quantisation table
        //-----------------------------------------------------------------------------
        else if (last_b == 0xFF && b == 0xdb)
        {
            //CheckPointIdx(i);
            printf("Section: DQT Table\n");
            int seg_start = i;
            uint16_t seg_len;
            get_word(seg_len, buf, i);
            CHECK_ENOUGH_BUF(seg_start+seg_len-1, len, buf, 0);            
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
            printf("Section: DHT Table\n");
            CHECK_ENOUGH_BUF(seg_start+seg_len-1, len, buf, 0);
            m_dht.process(&buf[i], seg_len);
            i = seg_start + seg_len;
        }
        //-----------------------------------------------------------------------------
        // EOI: End of image
        //-----------------------------------------------------------------------------
        else if (last_b == 0xFF && b == 0xd9)
        {
            printf("Section: EOI\n");
            break;
        }
        //-----------------------------------------------------------------------------
        // SOS: Start of Scan Segment (SOS)
        //-----------------------------------------------------------------------------
        else if (last_b == 0xFF && b == 0xda)
        {
            printf("Section: SOS\n");
            //CheckPointIdx(i);
            int seg_start = i;

            if (m_mode == JPEG_UNSUPPORTED)
            {
                printf("ERROR: Unsupported JPEG mode\n");
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

                printf(" %d: ID=%x Table=%x\n", x, comp_id, comp_table);
            }

            // Skip bytes
            get_byte_no_assign(buf,i);
            get_byte_no_assign(buf,i);
            get_byte_no_assign(buf,i);

            i = seg_start + seg_len;
            CHECK_ENOUGH_BUF(seg_start + seg_len - 1, len, buf, 0);
            mcu_start = 1;
            b = 0;
            last_b = 0;
        }
        else if ( mcu_start ){
            
            //-----------------------------------------------------------------------
            // Process data segment
            //-----------------------------------------------------------------------
            m_bit_buffer.reset(len);
            m_bit_buffer.global_buf = buf;
            m_bit_buffer.global_buf_idx = i;
            m_bit_buffer.global_buf_len = len;

            while (i < len)
            {
                b = buf[i];
                if (m_bit_buffer.push(b))
                    i++;
                // Marker detected (reverse one byte)
                else
                {
                    i--;
                    // writeout_img();
                    printf("marker detected\n");
                    mcu_start = 0;
                    break;
                }
            }
            if (mcu_start == 0){
                m_bit_buffer.copy_out(buf);
            }else{
                m_bit_buffer.copy_out(buf);
            }
            dprintf("decode img\n");
            decode_done = DecodeImage();
        }
         else if (last_b == 0xFF && b == 0xc2)
        {
            printf("Section: SOF2\n");
            int seg_start = i;
            uint16_t seg_len;
            get_word(seg_len, buf, i);
            i = seg_start + seg_len;

            printf("ERROR: Progressive JPEG not supported\n");
            break; // ERROR: Not supported
        }
        else if (last_b == 0xFF && b == 0xdd)
        {
            printf("Section: DRI\n");
            int seg_start = i;
            uint16_t seg_len;
            get_word(seg_len, buf, i);
            i = seg_start + seg_len;            
        }
        else if (last_b == 0xFF && b >= 0xd0 && b <= 0xd7)
        {
            printf("Section: RST%d\n", b - 0xd0);
            int seg_start = i;
            uint16_t seg_len;
            get_word(seg_len, buf, i);
            i = seg_start + seg_len;
        }
        else if (last_b == 0xFF && b >= 0xe0 && b <= 0xef)
        {
            printf("Section: APP%d\n", b - 0xe0);
            int seg_start = i;
            uint16_t seg_len;
            get_word(seg_len, buf, i);
            i = seg_start + seg_len;
            RollLog();
        }
        else if (last_b == 0xFF && b == 0xfe)
        {
            printf("Section: COM\n");
            int seg_start = i;
            uint16_t seg_len;
            get_word(seg_len, buf, i);
            i = seg_start + seg_len;
        }

        last_b = b;
    }
    // int sum = 0;
    // // printf("done with mcu_cnt size %d \n", mcu_cnt.size());
    // for (size_t i = 0; i < mcu_cnt.size(); ++i) {
    //     sum += 3*(mcu_cnt[i])+6;
    //     NEW_TOKEN(mcu_token, new_token);
    //     new_token->delay = 3*(mcu_cnt[i]) + 6;
    //     pvarlatency.tokens.push_back(new_token);
    // }
    // // printf("avrage eob %f , sum eob %d \n", (double)sum/mcu_cnt.size(), sum);
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

#endif