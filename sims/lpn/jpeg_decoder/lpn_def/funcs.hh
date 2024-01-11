
#ifndef __JPEG_DECODER_LPN_FUNCS__
#define __JPEG_DECODER_LPN_FUNCS__
#include <functional>
#include "places.hh"

std::function<int()> con_delay(int constant){
    auto delay = [&, constant]() -> int{
        return constant;
    };
};

int take_1_token(){
    return 1;
}

int take_0_token(){
    return 0;
}

int take_4_token(){
    return 4;
}

std::function<int()> take_some_token(int constant){
    auto num_tokens = [&, constant]() -> int{
        return constant;
    };
};

int mcu_delay(){
    return pvarlatency.tokens[0]->delay;
};

std::function<void(base_place*)> pass_empty_token() {
    auto output_token = [&](base_place* output_place) -> void {
        NEW_TOKEN(empty_token, new_token);
        output_place->push_token(new_token);
    };
    return output_token;
};

std::function<void(base_place*)> pass_4_empty_token() {
    auto output_token = [&](base_place* output_place) -> void {
        for(int i=0; i<4; i++){
            NEW_TOKEN(empty_token, new_token);
            output_place->push_token(new_token);
        }
    };
    return output_token;
};

#endif
