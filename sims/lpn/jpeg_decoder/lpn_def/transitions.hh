#ifndef __JPEG_DECODER_LPN_TRANSITIONS__
#define __JPEG_DECODER_LPN_TRANSITIONS__
#include "../../lpn_common/place_transition.hh"
#include "places.hh"
#include "funcs.hh"
Transition t1 = {
    .id = "1",
    .delay_f = conDelay(0),
    .p_input = {&ptasks, &p8},
    .pi_w = {take1Token, take1Token},
    .p_output = {&p7},
    .po_w = {passEmptyToken()},
};

Transition t0 = {
    .id = "0",
    .delay_f = mcuDelay,
    .p_input = {&p7, &p4, &pvarlatency},
    .pi_w = {take1Token, take1Token, take1Token},
    .p_output = {&p0, &p8},
    .po_w = {passEmptyToken(),passEmptyToken()},
};

Transition t5 = {
    .id = "5",
    .delay_f = conDelay(65),
    .p_input = {&p1, &p2, &p3},
    .pi_w = {take1Token, take1Token, take1Token},
    .p_output = {&pbefore_done, &p6},
    .po_w = {passEmptyToken(),passEmptyToken()},
};

Transition tfinal = {
    .id = "final",
    .delay_f = conDelay(0),
    .p_input = {&pbefore_done},
    .pi_w = {take4Token},
    .p_output = {&pdone},
    .po_w = {passEmptyToken()},
};

Transition t4 = {
    .id = "4",
    .delay_f = conDelay(66),
    .p_input = {&p0, &p22, &p6},
    .pi_w = {take1Token, take1Token, take4Token},
    .pi_w_threshold = {0, 0, 2},
    .p_output = {&p3, &p20, &p4},
    .po_w = {pass4EmptyToken(),pass4EmptyToken(), passEmptyToken()},
};

Transition t3 = {
    .id = "3",
    .delay_f = conDelay(66),
    .p_input = {&p0, &p21, &p6},
    .pi_w = {take1Token, take4Token, take0Token},
    .pi_w_threshold = {0, 0, 2},
    .p_output = {&p2, &p22, &p4},
    .po_w = {pass4EmptyToken(),passEmptyToken(), passEmptyToken()},
};

Transition t2 = {
    .id = "2",
    .delay_f = conDelay(66),
    .p_input = {&p0, &p20, &p6},
    .pi_w = {take1Token, take1Token, take0Token},
    .pi_w_threshold = {0, 0, 2},
    .p_output = {&p1, &p21, &p4},
    .po_w = {passEmptyToken(),passEmptyToken(), passEmptyToken()},
};

#endif
