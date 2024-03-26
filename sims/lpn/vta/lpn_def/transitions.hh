#ifndef __VTA_TRANSITIONS__
#define __VTA_TRANSITIONS__
#include <stdlib.h>
#include <functional>
#include "sims/lpn/lpn_common/place_transition.hh"
#include "places.hh"
#include "lpn.hh"
Transition t13 = {
    .id = "t13",
    .delay_f = con_delay(1),
    .p_input = {&plaunch},
    .pi_w = {take_1_token()},
    .pi_w_threshold = {0},
    .p_output = {&psReadCmd},
    .po_w = {output_insn_read_cmd()},
};
Transition t9 = {
    .id = "t9",
    .delay_f = delay_t9(),
    .p_input = {&psReadCmd, &pcontrol, &pnumInsn},
    .pi_w = {take_1_token(), take_1_token(), take_readLen(psReadCmd)},
    .pi_w_threshold = {0, 0, 0},
    .p_output = {&psDrain, &pcontrol_prime},
    .po_w = {pass_var_token_readLen(pnumInsn), pass_empty_token()},
};
Transition t12 = {
    .id = "t12",
    .delay_f = con_delay(1),
    .p_input = {&pcontrol_prime},
    .pi_w = {take_1_token()},
    .pi_w_threshold = {0},
    .p_output = {&pcontrol},
    .po_w = {pass_empty_token()},
};
Transition t14 = {
    .id = "t14",
    .delay_f = con_delay(1),
    .p_input = {&psDrain, &pload_cap},
    .pi_w = {take_1_token(), take_1_token()},
    .pi_w_threshold = {0, 0},
    .pi_guard = {take_opcode_token(psDrain, (int)ALL_ENUM::LOAD), empty_guard()},
    .p_output = {&pload_inst_q},
    .po_w = {pass_token(psDrain, 1)},
};
Transition t15 = {
    .id = "t15",
    .delay_f = con_delay(1),
    .p_input = {&psDrain, &pcompute_cap},
    .pi_w = {take_1_token(), take_1_token()},
    .pi_w_threshold = {0, 0},
    .pi_guard = {take_opcode_token(psDrain, (int)ALL_ENUM::COMPUTE), empty_guard()},
    .p_output = {&pcompute_inst_q},
    .po_w = {pass_token(psDrain, 1)},
};
Transition t16 = {
    .id = "t16",
    .delay_f = con_delay(1),
    .p_input = {&psDrain, &pstore_cap},
    .pi_w = {take_1_token(), take_1_token()},
    .pi_w_threshold = {0, 0},
    .pi_guard = {take_opcode_token(psDrain, (int)ALL_ENUM::STORE), empty_guard()},
    .p_output = {&pstore_inst_q},
    .po_w = {pass_token(psDrain, 1)},
};
Transition tload_launch = {
    .id = "tload_launch",
    .delay_f = con_delay(0),
    .p_input = {&pload_inst_q, &pcompute2load},
    .pi_w = {take_1_token(), take_dep_pop_next(pload_inst_q)},
    .pi_w_threshold = {0, 0},
    .p_output = {&pload_process},
    .po_w = {pass_token(pload_inst_q, 1)},
};
Transition tload_done = {
    .id = "load_done",
    .delay_f = delay_load(pload_process),
    .p_input = {&pload_process},
    .pi_w = {take_1_token()},
    .pi_w_threshold = {0},
    .p_output = {&pload_done, &pload2compute, &pload_cap},
    .po_w = {pass_empty_token(), output_dep_push_next(pload_process), pass_empty_token()},
};
Transition tstore_launch = {
    .id = "store_launch",
    .delay_f = con_delay(0),
    .p_input = {&pstore_inst_q, &pcompute2store},
    .pi_w = {take_1_token(), take_dep_pop_prev(pstore_inst_q)},
    .pi_w_threshold = {0, 0},
    .p_output = {&pstore_process},
    .po_w = {pass_token(pstore_inst_q, 1)},
};
Transition tstore_done = {
    .id = "store_done",
    .delay_f = delay_store(pstore_process),
    .p_input = {&pstore_process},
    .pi_w = {take_1_token()},
    .pi_w_threshold = {0},
    .p_output = {&pstore_done, &pstore2compute, &pstore_cap},
    .po_w = {pass_empty_token(), output_dep_push_prev(pstore_process), pass_empty_token()},
};
Transition tcompute_launch = {
    .id = "compute_launch",
    .delay_f = con_delay(0),
    .p_input = {&pcompute_inst_q, &pstore2compute, &pload2compute},
    .pi_w = {take_1_token(), take_dep_pop_next(pcompute_inst_q), take_dep_pop_prev(pcompute_inst_q)},
    .pi_w_threshold = {0, 0, 0},
    .p_output = {&pcompute_process},
    .po_w = {pass_token(pcompute_inst_q, 1)},
};
Transition tcompute_done = {
    .id = "compute_done",
    .delay_f = delay_compute(pcompute_process),
    .p_input = {&pcompute_process},
    .pi_w = {take_1_token()},
    .pi_w_threshold = {0},
    .p_output = {&pcompute_done, &pcompute2load, &pcompute2store, &pcompute_cap},
    .po_w = {pass_empty_token(), output_dep_push_prev(pcompute_process), output_dep_push_next(pcompute_process), pass_empty_token()},
};
#endif