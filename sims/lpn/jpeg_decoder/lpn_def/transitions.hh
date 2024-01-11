#ifndef __JPEG_DECODER_LPN_TRANSITIONS__
#define __JPEG_DECODER_LPN_TRANSITIONS__
#include "../../lpn_common/place_transition.hh"
#include "places.hh"
#include "funcs.hh"
transition t1 = {
    .id = "1",
    .delay_f = con_delay(0),
    .p_input = {&ptasks, &p8},
    .pi_w = {take_1_token, take_1_token},
    .p_output = {&p7},
    .po_w = {pass_empty_token()},
};

transition t0 = {
    .id = "0",
    .delay_f = mcu_delay,
    .p_input = {&p7, &p4, &pvarlatency},
    .pi_w = {take_1_token, take_1_token, take_1_token},
    .p_output = {&p0, &p8},
    .po_w = {pass_empty_token(),pass_empty_token()},
};

transition t5 = {
    .id = "5",
    .delay_f = con_delay(65),
    .p_input = {&p1, &p2, &p3},
    .pi_w = {take_1_token, take_1_token, take_1_token},
    .p_output = {&pdone, &p6},
    .po_w = {pass_empty_token(),pass_empty_token()},
};

transition t4 = {
    .id = "4",
    .delay_f = con_delay(66),
    .p_input = {&p0, &p22, &p6},
    .pi_w = {take_1_token, take_1_token, take_4_token},
    .p_output = {&p3, &p20, &p4},
    .pi_w_threshold = {0, 0, 2},
    .po_w = {pass_4_empty_token(),pass_4_empty_token(), pass_empty_token()},
};

transition t3 = {
    .id = "3",
    .delay_f = con_delay(66),
    .p_input = {&p0, &p21, &p6},
    .pi_w = {take_1_token, take_4_token, take_0_token},
    .p_output = {&p2, &p22, &p4},
    .pi_w_threshold = {0, 0, 2},
    .po_w = {pass_4_empty_token(),pass_empty_token(), pass_empty_token()},
};

transition t2 = {
    .id = "2",
    .delay_f = con_delay(66),
    .p_input = {&p0, &p20, &p6},
    .pi_w = {take_1_token, take_1_token, take_0_token},
    .p_output = {&p1, &p21, &p4},
    .pi_w_threshold = {0, 0, 2},
    .po_w = {pass_empty_token(),pass_empty_token(), pass_empty_token()},
};

#endif
