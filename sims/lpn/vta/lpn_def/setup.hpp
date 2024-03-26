#include <assert.h>
#include <fstream>
#include <iostream>
#include <string>
#include <regex>
#include <vector>
#include "sims/lpn/lpn_common/place_transition.hh"
#include "lpn.hh"
#include "places.hh"
#include "transitions.hh"
#include "token_types.hh"
using namespace std;

int translate(map<string, int>& name_dict, string key, int type){
    if (type==1) {
        if (name_dict.find(key) == name_dict.end()) {
            printf("name not found ! %s\n", key.c_str());
            assert(0);
        }else 
            return name_dict[key];
    }else{
        return atoi(key.c_str());
    }
}

void collect_insns(QT_type(token_class_ostxyuullupppp*)* tokens, string file){


    map<string, int> name_dict;
    name_dict["compute"] = static_cast<int>(ALL_ENUM::COMPUTE);
    name_dict["load"] = static_cast<int>(ALL_ENUM::LOAD);
    name_dict["store"] = static_cast<int>(ALL_ENUM::STORE);
    name_dict["inp"] = static_cast<int>(ALL_ENUM::INP);
    name_dict["wgt"] = static_cast<int>(ALL_ENUM::WGT);
    name_dict["gemm"] = static_cast<int>(ALL_ENUM::GEMM);
    name_dict["empty"] = static_cast<int>(ALL_ENUM::EMPTY);
    name_dict["alu"] = static_cast<int>(ALL_ENUM::ALU);
    name_dict["sync"] = static_cast<int>(ALL_ENUM::SYNC);
    name_dict["finish"] = static_cast<int>(ALL_ENUM::FINISH);
    name_dict["loadUop"] = static_cast<int>(ALL_ENUM::LOADUOP);
    
    ifstream fin(file);
    string s;
    while(getline(fin,s)){
        vector<string> dict;
        string compact = std::regex_replace(s, std::regex(" "), "");
        // printf("%s \n", compact.c_str());
        size_t last = 0; size_t next = 0; 
        while ((next = compact.find(",", last)) != string::npos) {
            dict.push_back(compact.substr(last, next-last));
            last = next + 1;
        }
        dict.push_back(compact.substr(last, last+1));
        NEW_TOKEN(token_class_ostxyuullupppp, new_token);
        new_token->opcode = translate(name_dict, dict[1], 1);
        new_token->subopcode = translate(name_dict, dict[2], 1);
        new_token->tstype = translate(name_dict, dict[3], 1);
        new_token->xsize = translate(name_dict, dict[4], 0);
        new_token->ysize = translate(name_dict, dict[5], 0);
        new_token->uop_begin = translate(name_dict, dict[6], 0);
        new_token->uop_end = translate(name_dict, dict[7], 0);
        new_token->lp_1 = translate(name_dict, dict[8], 0);
        new_token->lp_0 = translate(name_dict, dict[9], 0);
        new_token->use_alu_imm = translate(name_dict, dict[10], 0);
        new_token->pop_prev = translate(name_dict, dict[11], 0);
        new_token->pop_next = translate(name_dict, dict[12], 0);
        new_token->push_prev = translate(name_dict, dict[13], 0);
        new_token->push_next = translate(name_dict, dict[14], 0);
        tokens->push_back(new_token);
    }

    fin.close();
    return ;
}

void setup(){

  FILE* fp_read;
  // fp_read = fopen("/home/jiacma/npc/tvm/3rdparty/vta-hw/src/tsim/small_petri_net/tmp_dep_out", "r");
  fp_read = fopen("/home/jiacma/npc/tvm/3rdparty/vta-hw/src/tsim/faster_formal_petri/dump/Inr8fuTeIp_9419.insns.status", "r");

  int l2c, c2l, c2s, s2c;
  fscanf(fp_read, "%d,%d,%d,%d", &l2c, &c2l, &c2s, &s2c);
  fclose(fp_read);
  printf("read done");
  cout << l2c << " "<< c2l <<" " << c2s <<" " << s2c <<" " << endl;
  
  create_empty_queue(&(pcompute2store.tokens), c2s);  
  create_empty_queue(&(pcompute2load.tokens), c2l);  
  create_empty_queue(&(pstore2compute.tokens), s2c);  
  create_empty_queue(&(pload2compute.tokens), l2c);  
  create_empty_queue(&(pcompute_cap.tokens), 512);  
  create_empty_queue(&(pload_cap.tokens), 512);  
  create_empty_queue(&(pstore_cap.tokens), 512);  

  // string benchmark = "/home/jiacma/npc/tvm/3rdparty/vta-hw/src/tsim/small_petri_net/petri_sim.insns";
  string benchmark = "/home/jiacma/npc/tvm/3rdparty/vta-hw/src/tsim/faster_formal_petri/dump/Inr8fuTeIp_9419.insns";

  collect_insns(&(pnumInsn.tokens), benchmark);

  NEW_TOKEN(token_class_total_insn, numInstToken);
  numInstToken->total_insn = pnumInsn.tokens.size();
  int total_insn = pnumInsn.tokens.size();
  plaunch.tokens.push_back(numInstToken);

  create_empty_queue(&(pcontrol.tokens), 1);  
}
