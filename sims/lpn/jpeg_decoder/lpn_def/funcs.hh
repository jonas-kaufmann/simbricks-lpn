
#ifndef __JPEG_DECODER_LPN_FUNCS__
#define __JPEG_DECODER_LPN_FUNCS__
#include <functional>
#include "places.hh"

namespace lpnjpeg {
    uint64_t CYCLEPERIOD = 1'000'000 / 150;
}
std::function<uint64_t()> conDelay(int constant){
    auto delay = [&, constant]() -> int{
        return constant*lpnjpeg::CYCLEPERIOD;
    };
    return delay;
};

int take1Token(){
    return 1;
}

int take0Token(){
    return 0;
}

int take4Token(){
    return 4;
}

std::function<int()> takeSomeToken(int constant){
    auto num_tokens = [&, constant]() -> int{
        return constant;
    };
    return num_tokens;
};

uint64_t mcuDelay(){
    return pvarlatency.tokens[0]->delay*lpnjpeg::CYCLEPERIOD;
};

std::function<void(BasePlace*)> passEmptyToken() {
    auto output_token = [&](BasePlace* output_place) -> void {
        NEW_TOKEN(EmptyToken, new_token);
        output_place->pushToken(new_token);
    };
    return output_token;
};

std::function<void(BasePlace*)> pass4EmptyToken() {
    auto output_token = [&](BasePlace* output_place) -> void {
        for(int i=0; i<4; i++){
            NEW_TOKEN(EmptyToken, new_token);
            output_place->pushToken(new_token);
        }
    };
    return output_token;
};

#endif
