#ifndef __JPEG_DECODER_TOKEN_TYPES__
#define __JPEG_DECODER_TOKEN_TYPES__
#include"../../lpn_common/place_transition.hh"

CREATE_TOKEN_TYPE(
mcu_token,
int delay=0;
std::map<std::string, int>* asDictionary() override{
    std::map<std::string, int>* dict = new std::map<std::string, int>;
    dict->operator[]("delay")=delay; 
    return dict; 
})

#endif