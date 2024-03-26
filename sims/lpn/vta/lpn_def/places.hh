#ifndef __PLACE__
#define __PLACE__
#include "sims/lpn/lpn_common/place_transition.hh"
#include "token_types.hh"

extern Place<> pcontrol_prime;
extern Place<> pcontrol;
extern Place<token_class_insn_count> psReadCmd;
extern Place<token_class_ostxyuullupppp> pnumInsn;
extern Place<token_class_ostxyuullupppp> psDrain;
extern Place<token_class_total_insn> plaunch;
extern Place<> pload_cap;
extern Place<token_class_ostxyuullupppp> pload_inst_q;
extern Place<> pcompute_cap;
extern Place<token_class_ostxyuullupppp> pcompute_inst_q;
extern Place<> pstore_cap;
extern Place<token_class_ostxyuullupppp> pstore_inst_q;
extern Place<> pstore2compute;
extern Place<> pload2compute;
extern Place<token_class_ostxyuullupppp> pcompute_process;
extern Place<> pcompute2store;
extern Place<token_class_ostxyuullupppp> pstore_process;
extern Place<> pcompute2load;
extern Place<token_class_ostxyuullupppp> pload_process;
extern Place<> pcompute_done;
extern Place<> pstore_done;
extern Place<> pload_done;

#endif