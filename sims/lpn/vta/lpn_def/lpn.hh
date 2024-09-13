#ifndef __LPN__
#define __LPN__
#include <bits/stdint-uintn.h>
#include <stdlib.h>
#include <sys/types.h>
#include <functional>
#include <iostream>
#include <memory>
#include "sims/lpn/lpn_common/place_transition.hh"
#include "sims/lpn/vta/lpn_def/places.hh"
#include "sims/lpn/vta/include/lpn_req_map.hh"
#include "sims/lpn/vta/include/vta/driver.h"
#include "sims/lpn/vta/lpn_def/all_enum.hh"
#include "sims/lpn/vta/src/parse_tokens.hh"

namespace lpnvta {
    // in pico seconds
    // uint64_t CYCLEPERIOD = 1'000'000 / 1'000; // 1 GHz
    // uint64_t CYCLEPERIOD = 1'000'000 / 500; // 500Mhz
    uint64_t CYCLEPERIOD = 1'000'000 / 2000; // 2Ghz
}


int outstanding = 0;
int acc_bytes[10] = {0};
int end_times[10] = {0};
int start_times[10] = {0};


int issue_mem_op(int tag){
    auto& reqs = io_req_map[tag];
    int batch_id = -1;
    auto it = reqs.begin();
    
    int finish_round = 0;
    int finish_id = 0;

    while (it != reqs.end()) {
      auto req = it->get();
      if(req->issue == 3){
        acc_bytes[tag] += req->len;
        if(start_times[tag] == 0){
            start_times[tag] = req->complete_ts;
            end_times[tag] = req->complete_ts;
        }else{
            end_times[tag] = req->complete_ts;

        }
        // finish_id = req->id;
        // std::cout << "issue_mem_op tag finished: " << req->tag  << " addr " << req->addr << " len " << req->len << std::endl;
        finish_round = 1;
        it = reqs.erase(it);
        continue;
      }

      // wait 
      if(req->issue == 1 || req->issue == 2){
        return 1;
      }
      
      // next unissue is the next instruction
    //   if(finish_round == 1 && req->issue == 0 && finish_id != req->id){
      if(finish_round == 1 && req->issue == 0){
        return 0;
      }

      // 
      if(req->issue == 0){
        if(batch_id == -1){
          batch_id = req->id;
        }
        if(batch_id != req->id){
          return 1;
        }
        // std::cout << "issue_mem_op set to issue tag:" << req->tag  << " addr:" << req->addr << " len:" << req->len << std::endl;
        req->issue = 1;
        // finish_round = 0;
        // break;
        // I only issue one request
      }
      it++;
    }
    if(finish_round == 1){
        return 0;
    }
    return 1;
    // for(auto& req : req_queue){
    //     // to avoid issue already issued request
    //     if(req->acquired_len == req->len){
            
    //         continue;
    //     }

    //     if(batch_id == -1 && req->issue == 0){
    //         batch_id = req->id;
    //     }else{
    //         if(batch_id != -1 && batch_id != req->id){
    //             break;
    //         }
    //     }
    //     if(batch_id != -1 && req->issue == 0){
    //         std::cout << "issue_mem_op tag: " << req->tag  << " id " << batch_id << std::endl;
    //         req->issue = 1;
    //     }

    // }
}


#define NEW_REQ(id_, rw_, len_) \
    auto new_req = std::make_unique<LpnReq>(); \
    new_req->id = id_; \
    new_req->rw = rw_; \
    new_req->len = len_; \
    enqueueReq(lpn_req_map[id_], std::move(new_req));

std::function<int()> con_edge(int constant) {
    auto weight = [&, constant]() -> int {
        return constant;
    };
    return weight;
};
std::function<int()> take_1_token() {
    auto number_of_token = [&]() -> int {
        return 1;
    };
    return number_of_token;
};
template<typename T>
std::function<int()> take_dep_pop_prev(Place<T>& dependent_place) {
    auto number_of_token = [&]() -> int {
        auto key = dependent_place.tokens[0]->pop_prev;
        if (key == 1) {
            return 1;
        }
        else {
            return 0;
        }
    };
    return number_of_token;
};

// template<typename T>
// void log_fields(Place<T>& dependent_place) {
//     //std::cerr << dependent_place.id << std::endl;
//     //std::cerr << "opcode: " << dependent_place.tokens[0]->opcode << std::endl;
//     //std::cerr << "subopcode: " << dependent_place.tokens[0]->subopcode << std::endl;
//     //std::cerr << "tstype: " << dependent_place.tokens[0]->tstype << std::endl;
//     //std::cerr << "xsize: " << dependent_place.tokens[0]->xsize << std::endl;
//     //std::cerr << "ysize: " << dependent_place.tokens[0]->ysize << std::endl;
//     //std::cerr << "uop_begin: " << dependent_place.tokens[0]->uop_begin << std::endl;
//     //std::cerr << "uop_end: " << dependent_place.tokens[0]->uop_end << std::endl;
//     //std::cerr << "lp_0: " << dependent_place.tokens[0]->lp_0 << std::endl;
//     //std::cerr << "lp_1: " << dependent_place.tokens[0]->lp_1 << std::endl;
//     //std::cerr << "use_alu_imm: " << dependent_place.tokens[0]->use_alu_imm << std::endl;
// };

template<typename T>
std::function<int()> take_dep_pop_next(Place<T>& dependent_place) {
    auto number_of_token = [&]() -> int {
        auto key = dependent_place.tokens[0]->pop_next;
        if (key == 1) {
            return 1;
        }
        else {
            return 0;
        }
    };
    return number_of_token;
};
template<typename T>
std::function<int()> take_readLen(Place<T>& dependent_place) {
    auto number_of_token = [&]() -> int {
        auto key = dependent_place.tokens[0]->insn_count;
        return key;
    };
    return number_of_token;
};
std::function<int()> take_some_token(int number) {
    auto number_of_token = [&, number]() -> int {
        return number;
    };
    return number_of_token;
};
std::function<void(BasePlace*)> output_insn_read_cmd() {
    auto output_token = [&](BasePlace* output_place) -> void {
        auto total_insn = plaunch.tokens[0]->total_insn;
        auto max_insn = 128;
        auto ites = (total_insn / max_insn);
        auto remain = (total_insn % max_insn);
        for (int i = 0; i < ites; ++i) {
            NEW_TOKEN(token_class_insn_count, new_token);
            new_token->insn_count = max_insn;
            //std::cerr << "output_insn_read_cmd with length " << new_token->insn_count << std::endl;
            output_place->pushToken(new_token);
        }
        if (remain > 0) {
            NEW_TOKEN(token_class_insn_count, new_token);
            new_token->insn_count = remain;
            //std::cerr << "output_insn_read_cmd with length " << new_token->insn_count << std::endl;
            output_place->pushToken(new_token);
        }
    };
    return output_token;
};
template<typename T>
std::function<void(BasePlace*)> pass_var_token_readLen(Place<T>& from_place) {
    auto output_token = [&](BasePlace* output_place) -> void {
        auto num = psReadCmd.tokens[0]->insn_count;
        for (int i = 0; i < num; ++i) {
            auto token = from_place.tokens[i];
            output_place->pushToken(token);
        }
    };
    return output_token;
};
template<typename T>
std::function<void(BasePlace*)> pass_token(Place<T>& from_place, int num) {
    auto output_token = [&, num](BasePlace* output_place) -> void {
        for (int i = 0; i < num; ++i) {
            auto token = from_place.tokens[i];
            output_place->pushToken(token);
        }
    };
    return output_token;
};
std::function<void(BasePlace*)> pass_empty_token() {
    auto output_token = [&](BasePlace* output_place) -> void {
        NEW_TOKEN(EmptyToken, new_token);
        output_place->pushToken(new_token);
    };
    return output_token;
};
template<typename T>
std::function<void(BasePlace*)> output_dep_push_prev(Place<T>& dependent_place) {
    auto output_token = [&](BasePlace* output_place) -> void {
        auto direc = dependent_place.tokens[0]->push_prev;
        if (direc == 1) {
            NEW_TOKEN(EmptyToken, new_token);
            output_place->pushToken(new_token);
        }
    };
    return output_token;
};
template<typename T>
std::function<void(BasePlace*)> output_dep_push_next(Place<T>& dependent_place) {
    auto output_token = [&](BasePlace* output_place) -> void {
        auto direc = dependent_place.tokens[0]->push_next;
        if (direc == 1) {
            NEW_TOKEN(EmptyToken, new_token);
            output_place->pushToken(new_token);
        }
    };
    return output_token;
};
std::function<bool()> empty_guard() {
    auto guard = [&]() -> bool {
        return true;
    };
    return guard;
};

std::function<bool()> psDrain_is_empty() {
    auto guard = [&]() -> bool {
        if(psDrain.tokensLen() == 0){
            return true;
        }
        return false;
    };
    return guard;
};

template<typename T>
std::function<bool()> take_opcode_token(Place<T>& dependent_place, int opcode) {
    auto guard = [&, opcode]() -> bool {
        auto key = dependent_place.tokens[0]->opcode;
        if (key != opcode) {
            return false;
        }
        return true;
    };
    return guard;
};
template<typename T>
std::function<bool()> take_subopcode_token(Place<T>& dependent_place, int subopcode) {
    auto guard = [&, subopcode]() -> bool {
        auto key = dependent_place.tokens[0]->subopcode;
        if (key != subopcode) {
            return false;
        }
        return true;
    };
    return guard;
};

std::function<uint64_t()> delay_t9() {
    auto delay = [&]() -> uint64_t {
        int ret = issue_mem_op(LOAD_INSN);
        if(ret == 0){
            int tag = LOAD_INSN;
            uint64_t delay = lpnvta::CYCLEPERIOD*acc_bytes[tag]/8;
            uint64_t elapsed_time = end_times[tag] - start_times[tag];  
                //std::cerr << "tag: " << tag << " orig_delay:" << delay << " elapsed_time:" << elapsed_time << std::endl;

            if(delay > elapsed_time){
                delay = delay - elapsed_time;
            }else{
                delay = 0;
            }
            end_times[tag] = 0;
            start_times[tag] = 0;
            acc_bytes[tag] = 0;
            // the wire is 32bit, 4bytes
            return delay;
        }
        return  lpn::LARGE;
        // return lpnvta::CYCLEPERIOD*(21 + (2 * psReadCmd.tokens[0]->insn_count));
    };
    return delay;
};

template<typename T>
std::function<uint64_t()> delay_store(Place<T>& dependent_place) {
    auto delay = [&]() -> uint64_t {
        auto xsize = dependent_place.tokens[0]->xsize;
        auto ysize = dependent_place.tokens[0]->ysize;
        auto subopcode = dependent_place.tokens[0]->subopcode;
        if(subopcode == (int)ALL_ENUM::SYNC){
            num_instr--;
            return lpnvta::CYCLEPERIOD*2;
        }

        int ret = issue_mem_op(STORE_ID);
        if(ret == 0){
            int tag = STORE_ID;
            uint64_t delay = lpnvta::CYCLEPERIOD*acc_bytes[tag]/8;
            uint64_t elapsed_time = end_times[tag] - start_times[tag]; 
                //std::cerr << "tag: " << tag << " orig_delay:" << delay << " elapsed_time:" << elapsed_time << std::endl;

            if(delay > elapsed_time){
                delay = delay - elapsed_time;
            }else{
                delay = 0;
            }
            end_times[tag] = 0;
            start_times[tag] = 0;
            acc_bytes[tag] = 0;
            // the wire is 32bit, 4bytes
            return delay;
        }
        return  lpn::LARGE;
        // return lpnvta::CYCLEPERIOD*((27 * (xsize /(double) 8)) * ysize);
    };
    return delay;
};


std::function<uint64_t()> con_delay(uint64_t constant) {
    auto delay = [&, constant]() -> uint64_t {
        return lpnvta::CYCLEPERIOD*constant;
    };
    return delay;
};

template<typename T>
std::function<int()> take_start_token(Place<T>& dependent_place) {
    auto number_of_token = [&]() -> int {
        return 1;
    };
    return number_of_token;
};

template<typename T>
std::function<void(BasePlace*)> output_launch_token(Place<T>& dependent_place) {
    auto output_token = [&](BasePlace* output_place) -> void {
        NEW_TOKEN(token_class_total_insn, launch_token);
        launch_token->total_insn = pstart.tokens[0]->insn_size/16;
        output_place->pushToken(launch_token);
        //std::cerr << "output_launch_token with length " << launch_token->total_insn << std::endl;
        return;

        auto& reqs = ctl_nb_lpn.req_matcher[LOAD_INSN].reqs;
        auto& front = reqs.front();
        assert(front->acquired_len == front->len);
        //std::cerr << "output_launch_token with length " << front->len << std::endl;
        auto insn_len = front->len/16;
        for (int i = 0; i < insn_len; i++) {
            output_place->pushToken(MakeLaunchToken());
        }
        // output_place->pushToken(MakeLaunchToken());
    };
    return output_token;
};

template<typename T>
std::function<void(BasePlace*)> output_pnum_insn(Place<T>& dependent_place) {
    auto output_token = [&](BasePlace* output_place) -> void {
        auto num = psReadCmd.tokens[0]->insn_count;
        auto& reqs = ctl_nb_lpn.req_matcher[LOAD_INSN].reqs;
        auto& front = reqs.front();
        assert(front->acquired_len == front->len);
        auto insn_len = front->len/16;
        // //std::cerr << "output_pnum_insn with length " << insn_len << " num:" << num << std::endl;
        assert(insn_len == num);
        for (int i = 0; i < insn_len; i++) {
            sixteen_byte_insn insn;
            insn.data1_ = *((uint64_t*)front->buffer+i*2);
            insn.data2_ = *((uint64_t*)front->buffer+i*2+1);
            output_place->pushToken(MakeNumInsnToken(&insn));
        }
        reqs.erase(reqs.begin());
        // dequeueReq(io_req_map[LOAD_INSN]);
    };
    return output_token;
};


template<typename T>
std::function<uint64_t()> delay_start(Place<T>& dependent_place) {
    auto delay = [&]() -> uint64_t {
        auto dram_addr = dependent_place.tokens[0]->addr;
        auto req_len = dependent_place.tokens[0]->insn_size; 
        return 0;

        // int ret = issue_mem_op(LOAD_INSN);
        // //std::cerr << "delay_start ret value  " << ret << std::endl;
        // if(ret == 0){
        //     return 0;
        // }
        // return  lpn::LARGE;
    };
    return delay;
}

template<typename T>
std::function<uint64_t()> delay_load(Place<T>& dependent_place) {
    auto delay = [&]() -> uint64_t {
        uint64_t insn_ptr = (uint64_t)&(dependent_place.tokens[0]->insn);
        auto subopcode = dependent_place.tokens[0]->subopcode;
        auto tstype = dependent_place.tokens[0]->tstype;
        auto xsize = dependent_place.tokens[0]->xsize;
        auto ysize = dependent_place.tokens[0]->ysize;

        if (subopcode == (int)ALL_ENUM::SYNC) {
          num_instr--;
          return lpnvta::CYCLEPERIOD * 2;
        }

        int tag = 0, rw_req = 0;
        if (tstype == (int)ALL_ENUM::INP) {
          // uint32_t req_len = (((((xsize * ysize) * 1) * 16) * 8)/64*8);
          tag = LOAD_INP_ID;
          rw_req = READ_REQ;
        }
        if (tstype == (int)ALL_ENUM::WGT) {
          tag = LOAD_WGT_ID;
          rw_req = READ_REQ;
        }
        int ret = issue_mem_op(tag);
        if(ret == 0){

            uint64_t delay = lpnvta::CYCLEPERIOD*acc_bytes[tag]/8;
            uint64_t elapsed_time = end_times[tag] - start_times[tag]; 
                //std::cerr << "tag: " << tag << " orig_delay:" << delay << " elapsed_time:" << elapsed_time << std::endl;

            if(delay > elapsed_time){
                delay = delay - elapsed_time;
            }else{
                delay = 0;
            }
            end_times[tag] = 0;
            start_times[tag] = 0;
            acc_bytes[tag] = 0;

            
            // the wire is 32bit, 4bytes
            return delay;
        }
        return  lpn::LARGE;
    };
    return delay;
};
template<typename T>
uint64_t delay_gemm(Place<T>& dependent_place) {
    auto uop_begin = dependent_place.tokens[0]->uop_begin;
    auto uop_end = dependent_place.tokens[0]->uop_end;
    auto lp_0 = dependent_place.tokens[0]->lp_0;
    auto lp_1 = dependent_place.tokens[0]->lp_1;
    num_instr--;
    return lpnvta::CYCLEPERIOD*((1 + 5) + (((uop_end - uop_begin) * lp_1) * lp_0));
};
template<typename T>
uint64_t delay_loadUop(Place<T>& dependent_place) {
    // //std::cerr << "delay_loadUop" << std::endl;
    int ret = issue_mem_op(LOAD_UOP_ID);

    if(ret == 0){
        int tag = LOAD_UOP_ID;
        uint64_t delay = lpnvta::CYCLEPERIOD*acc_bytes[tag]/8;
        uint64_t elapsed_time = end_times[tag] - start_times[tag];  
            //std::cerr << "tag: " << tag << " orig_delay:" << delay << " elapsed_time:" << elapsed_time << std::endl;

        if(delay > elapsed_time){
            delay = delay - elapsed_time;
        }else{
            delay = 0;
        }
        end_times[tag] = 0;
        start_times[tag] = 0;
        acc_bytes[tag] = 0;
        
        // the wire is 32bit, 4bytes
        return delay;
    }
    return  lpn::LARGE;
};
template<typename T>
uint64_t delay_loadAcc(Place<T>& dependent_place) {
    int ret = issue_mem_op(LOAD_ACC_ID);
    if(ret == 0){
        int tag = LOAD_ACC_ID;
        uint64_t delay = lpnvta::CYCLEPERIOD*acc_bytes[tag]/8;
        uint64_t elapsed_time = end_times[tag] - start_times[tag];  
            //std::cerr << "tag: " << tag << " orig_delay:" << delay << " elapsed_time:" << elapsed_time << std::endl;

        if(delay > elapsed_time){
            delay = delay - elapsed_time;
        }else{
            delay = 0;
        }
        end_times[tag] = 0;
        start_times[tag] = 0;
        acc_bytes[tag] = 0;
        // the wire is 32bit, 4bytes
        return delay;
    }
   return  lpn::LARGE;
};

template<typename T>
uint64_t delay_alu(Place<T>& dependent_place) {
    auto uop_begin = dependent_place.tokens[0]->uop_begin;
    auto uop_end = dependent_place.tokens[0]->uop_end;
    auto lp_0 = dependent_place.tokens[0]->lp_0;
    auto lp_1 = dependent_place.tokens[0]->lp_1;
    auto use_alu_imm = dependent_place.tokens[0]->use_alu_imm;
    num_instr--;
    return lpnvta::CYCLEPERIOD*((1 + 5) + ((((uop_end - uop_begin) * lp_1) * lp_0) * (2 - use_alu_imm)));
};
template<typename T>
std::function<uint64_t()> delay_compute(Place<T>& dependent_place) {
    auto delay = [&]() -> uint64_t {
        auto subopcode = dependent_place.tokens[0]->subopcode;
        if (subopcode == (int)ALL_ENUM::SYNC) {
          num_instr--;
          return lpnvta::CYCLEPERIOD * (1 + 1);
        }
        if (subopcode == (int)ALL_ENUM::ALU) {
            return delay_alu(dependent_place);
        }
        if (subopcode == (int)ALL_ENUM::GEMM) {
            return delay_gemm(dependent_place);
        }
        if (subopcode == (int)ALL_ENUM::LOADACC) {
            return delay_loadAcc(dependent_place);
        }
        if (subopcode == (int)ALL_ENUM::LOADUOP) {
            return delay_loadUop(dependent_place);
        }
        return 0;
    };
    return delay;
};
#endif