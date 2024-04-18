#ifndef __PLACE__
#define __PLACE__
#include "places.hh"
#include "token_types.hh"

Place<token_start> pstart("pstart");

Place<> pcontrol_prime("pcontrol_prime");
Place<> pcontrol("pcontrol");
Place<token_class_insn_count> psReadCmd("psReadCmd");
Place<token_class_ostxyuullupppp> pnumInsn("pnumInsn");
Place<token_class_ostxyuullupppp> psDrain("psDrain");
Place<token_class_total_insn> plaunch("plaunch");
Place<> pload_cap("pload_cap");
Place<token_class_ostxyuullupppp> pload_inst_q("pload_inst_q");
Place<> pcompute_cap("pcompute_cap");
Place<token_class_ostxyuullupppp> pcompute_inst_q("pcompute_inst_q");
Place<> pstore_cap("pstore_cap");
Place<token_class_ostxyuullupppp> pstore_inst_q("pstore_inst_q");
Place<> pstore2compute("pstore2compute");
Place<> pload2compute("pload2compute");
Place<token_class_ostxyuullupppp> pcompute_process("pcompute_process");
Place<> pcompute2store("pcompute2store");
Place<token_class_ostxyuullupppp> pstore_process("pstore_process");
Place<> pcompute2load("pcompute2load");
Place<token_class_ostxyuullupppp> pload_process("pload_process");
Place<> pcompute_done("pcompute_done");
Place<> pstore_done("pstore_done");
Place<> pload_done("pload_done");

#endif