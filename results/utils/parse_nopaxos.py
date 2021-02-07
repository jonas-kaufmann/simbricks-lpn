import json
import re
import os

def parse_nopaxos_run(num_c, seq, path):
    
    ret = {}
    ret['throughput'] = None
    ret['latency'] = None

    tp_pat = re.compile(r'(.*)Completed *([0-9\.]*) requests in *([0-9\.]*) seconds')
    lat_pat = re.compile(r'(.*)Average latency is *([0-9\.]*) ns(.*)')
    if not os.path.exists(path):
        return ret
    
    f_log = open(path, 'r')
    log = json.load(f_log)

    total_tput = 0
    total_avglat = 0
    for i in range(num_c):
        sim_name = f'host.client.{i}'
        #print(sim_name)
        
        # in this host log stdout
        for j in log["sims"][sim_name]["stdout"]:
            #print(j)
            m_t = tp_pat.match(j)
            m_l = lat_pat.match(j)
            if m_l:
                #print(j)
                lat = float(m_l.group(2)) / 1000 # us latency
                #print(lat)
                total_avglat += lat
                
            if m_t:
                
                n_req = float(m_t.group(2))
                n_time = float(m_t.group(3))
                total_tput += n_req/n_time


    avglat = total_avglat/num_c
    #print(avglat)    
    #print(total_tput)
    ret['throughput'] = total_tput
    ret['latency'] = avglat

    return ret