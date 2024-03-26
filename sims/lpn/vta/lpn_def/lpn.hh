#ifndef __LPN__
#define __LPN__
#include <bits/stdint-uintn.h>
#include <stdlib.h>
#include <sys/types.h>
#include <functional>
#include <iostream>
#include "sims/lpn/lpn_common/place_transition.hh"
#include "sims/lpn/vta/lpn_def/places.hh"
#include "sims/lpn/vta/include/lpn_req_map.hh"
#include "sims/lpn/vta/include/vta/driver.h"
#include "sims/lpn/vta/lpn_def/all_enum.hh"

namespace lpnvta {
    uint64_t CYCLEPERIOD = 1'000'000 / 150;
}

#define NEW_REQ(id_, rw_, len_) \
    auto new_req = std::make_unique<LpnReq>(); \
    new_req->id = id_; \
    new_req->rw = rw_; \
    new_req->len = len_; \
    enqueueReq(lpn_req_map[id_], new_req);

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
//     std::cerr << dependent_place.id << std::endl;
//     std::cerr << "opcode: " << dependent_place.tokens[0]->opcode << std::endl;
//     std::cerr << "subopcode: " << dependent_place.tokens[0]->subopcode << std::endl;
//     std::cerr << "tstype: " << dependent_place.tokens[0]->tstype << std::endl;
//     std::cerr << "xsize: " << dependent_place.tokens[0]->xsize << std::endl;
//     std::cerr << "ysize: " << dependent_place.tokens[0]->ysize << std::endl;
//     std::cerr << "uop_begin: " << dependent_place.tokens[0]->uop_begin << std::endl;
//     std::cerr << "uop_end: " << dependent_place.tokens[0]->uop_end << std::endl;
//     std::cerr << "lp_0: " << dependent_place.tokens[0]->lp_0 << std::endl;
//     std::cerr << "lp_1: " << dependent_place.tokens[0]->lp_1 << std::endl;
//     std::cerr << "use_alu_imm: " << dependent_place.tokens[0]->use_alu_imm << std::endl;
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
        auto max_insn = 8;
        auto ites = (total_insn / max_insn);
        auto remain = (total_insn % max_insn);
        for (int i = 0; i < ites; ++i) {
            NEW_TOKEN(token_class_insn_count, new_token);
            new_token->insn_count = max_insn;
            output_place->pushToken(new_token);
        }
        if (remain > 0) {
            NEW_TOKEN(token_class_insn_count, new_token);
            new_token->insn_count = remain;
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
        return lpnvta::CYCLEPERIOD*(21 + (2 * psReadCmd.tokens[0]->insn_count));
    };
    return delay;
};
template<typename T>
std::function<uint64_t()> delay_store(Place<T>& dependent_place) {
    auto delay = [&]() -> uint64_t {
        auto xsize = dependent_place.tokens[0]->xsize;
        auto ysize = dependent_place.tokens[0]->ysize;
        return lpnvta::CYCLEPERIOD*((27 * (xsize / 8)) * ysize);
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
std::function<uint64_t()> delay_load(Place<T>& dependent_place) {
    auto delay = [&]() -> uint64_t {
        auto subopcode = dependent_place.tokens[0]->subopcode;
        auto tstype = dependent_place.tokens[0]->tstype;
        auto xsize = dependent_place.tokens[0]->xsize;
        auto ysize = dependent_place.tokens[0]->ysize;
        if (subopcode == (int)ALL_ENUM::SYNC) {
            return lpnvta::CYCLEPERIOD*2;
        }
        if (tstype == (int)ALL_ENUM::INP) {
            auto& front = frontReq(lpn_req_map[LOAD_INP_ID]);
            if(front == nullptr){
                uint32_t req_len = (((((xsize * ysize) * 1) * 16) * 8)/64*8);
                NEW_REQ(LOAD_INP_ID, READ_REQ, req_len);
                return lpn::LARGE;
            }else{
                std::cerr << "front->acquired_len: " << front->acquired_len << " need:" << front->len << std::endl;
                if(front->acquired_len >= front->len){
                    std::cerr << " dequeue once " << std::endl;
                    dequeueReq(lpn_req_map[LOAD_INP_ID]);
                    //can fire instantly
                    return 0;
                }
            }
            return lpn::LARGE;
            // return lpnvta::CYCLEPERIOD*((1 + 21) + (((((xsize * ysize) * 1) * 16) * 8) / 64));
        }
        if (tstype == (int)ALL_ENUM::WGT) {
            return lpnvta::CYCLEPERIOD*((1 + 21) + (((((xsize * ysize) * 16) * 16) * 8) / 64));
        }
        return 0;
    };
    return delay;
};
template<typename T>
uint64_t delay_gemm(Place<T>& dependent_place) {
    auto uop_begin = dependent_place.tokens[0]->uop_begin;
    auto uop_end = dependent_place.tokens[0]->uop_end;
    auto lp_0 = dependent_place.tokens[0]->lp_0;
    auto lp_1 = dependent_place.tokens[0]->lp_1;
    return lpnvta::CYCLEPERIOD*((1 + 5) + (((uop_end - uop_begin) * lp_1) * lp_0));
};
template<typename T>
uint64_t delay_loadUop(Place<T>& dependent_place) {
    auto xsize = dependent_place.tokens[0]->xsize;
    auto ysize = dependent_place.tokens[0]->ysize;
    return lpnvta::CYCLEPERIOD*((1 + 21) + ((xsize * ((ysize + 2) - (ysize % 2))) / 2));
};
template<typename T>
uint64_t delay_loadAcc(Place<T>& dependent_place) {
    auto xsize = dependent_place.tokens[0]->xsize;
    auto ysize = dependent_place.tokens[0]->ysize;
    return lpnvta::CYCLEPERIOD*((1 + 21) + ((xsize * 8) * ysize));
};
template<typename T>
uint64_t delay_alu(Place<T>& dependent_place) {
    auto uop_begin = dependent_place.tokens[0]->uop_begin;
    auto uop_end = dependent_place.tokens[0]->uop_end;
    auto lp_0 = dependent_place.tokens[0]->lp_0;
    auto lp_1 = dependent_place.tokens[0]->lp_1;
    auto use_alu_imm = dependent_place.tokens[0]->use_alu_imm;
    return lpnvta::CYCLEPERIOD*((1 + 5) + ((((uop_end - uop_begin) * lp_1) * lp_0) * (2 - use_alu_imm)));
};
template<typename T>
std::function<uint64_t()> delay_compute(Place<T>& dependent_place) {
    auto delay = [&]() -> uint64_t {
        auto subopcode = dependent_place.tokens[0]->subopcode;
        if (subopcode == (int)ALL_ENUM::SYNC) {
            return lpnvta::CYCLEPERIOD*(1 + 1);
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