#define MY_NODE_ID 0
#define HALF 32
#define USE_KERNEL 1
#define USE_PIPES 1

#define CORES 19
#define CONTS 32
#define FLOWS 2
#define FREQ_STEP_DES 12
#define MAXSTEP 23

#include <vector>
#include <sstream>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <string>
#include <cstdlib>
#include <time.h>
#include <cmath>
#include <map>
#include <sys/types.h>
#include <sys/stat.h>

/*
 * Variables for controller
 */
// Current core mapping
int core_state[HALF*2];            
std::map<int, std::vector<int>> downstream_conts[FLOWS];
 
// Variables calculated in read_stats()
bool flow_slowdown[FLOWS];
bool flow_okay[FLOWS];
bool exec_slowdown[CONTS];
bool queue_slowdown[CONTS];
bool queue_okay[CONTS];
bool exec_okay[CONTS];

// Extra variables needed for deciding what to upscale
int cpus[CONTS];
int cidx = 0;
bool upscaled[CONTS];
float maxViolation[CONTS];
int cluster_freqStep[CONTS];    // cluster to freqStep

// Setup and node state
int num_containers;
int num_flows;
int num_clusters;
// Variables set in initialize()
std::vector<int> container_flow_array[FLOWS];            // Array of all active containers in any given flow. Indexed by flowID.
float exec_desired[FLOWS][CONTS], input_desired[FLOWS][CONTS];    // Array of desired input and exec time for each container, for each flow.
float exec_got[FLOWS][CONTS];
float exec_metric[FLOWS][CONTS];
float exec_time[FLOWS][CONTS][HALF];
int downstream[FLOWS][3];
struct timespec last_upscale_time[FLOWS];

std::string container_name_array[CONTS];        // indexed by containerID
int place[CONTS], cluster_id[CONTS];            // indexed by containerID
std::vector<uint32_t> cluster_to_conts[CONTS];
int cont_to_cluster[CONTS];

// File names for the various things needed
std::string _cname[CONTS];    // Container name
std::string _snames[CONTS];   // Stats file names
std::string _fnames[CONTS];   // Frequency file names

// Resource change
std::string stats1 = "/home/cc/paper_setup/shared/";
std::string cpufreq1 = "/sys/devices/system/cpu/cpu";
std::string cpufreq2 = "/cpufreq/scaling_setspeed";
uint32_t FREQ_ARRAY[24] = {800000, 900000, 1000000, 1100000, 1200000, 1300000, 1400000, 1500000, 1600000, 1700000, 1800000, 1900000, 2000000, 2100000, 2200000, 2300000, 2400000, 2500000, 2600000, 2700000, 2800000, 2900000, 3000000, 3100000};

// Things for defining whether resource changes have occured, these are used by the actual resource update fns
bool changed_core[CONTS];
int st_loc[FLOWS];

//=========================================================================================================================================================//
/*
 * Structure functions. Tells you upstream/downstream nodes/other node to edge conversion as needed.
 */
//=========================================================================================================================================================//
/*
 * Given a node and a flow, return the incoming edge which will be used to determine the downstream edges
 */
int node_to_edge(int node, int flow) {
    // This is going to be different for each flow.
    if(flow == 0) {        // ReadUserTimeline
        switch(node) {
            case 0: return -1;
            case 1: return 0;
            case 2: return 1;
            case 3: return 2;
            case 4: return 3;
            case 5: return 4;
            case 6: return 5;
        }
    }
    return -1;
}

/*
 * Given an edge and a flow, return a list of 3 downstream containers on this node
 */
void downstream_on_all_node(uint32_t edge, int flow) {
    auto it = downstream_conts[flow].find(edge);
    if(it == downstream_conts[flow].end()){
	downstream[flow][0] = downstream[flow][1] = downstream[flow][2] = -1;
    }
    else {
	int i=0;
	for(i=0; i<3 && i<it->second.size(); i++)
	    downstream[flow][i] = (it->second)[i];
	for(int j=i; j<3; j++)
	    downstream[flow][j] = -1;
    }
}

/*
 * Functions for detecting things on the slow path 
 */

void read_stats_file() {
  for(int i=0; i<num_clusters; i++) {
    queue_slowdown[i] = exec_slowdown[i] = false;
    queue_okay[i] = exec_okay[i] = true;
  }
  for(int i=0; i<FLOWS; i++) {
    for(int j=0; j<num_clusters; j++) {
      exec_got[i][j] = 0;
    }
    flow_slowdown[i] = false;
    flow_okay[i] = true;
  }
  
  // Read all the stats files.
  for(int i=0; i<num_containers; i++) {
    std::ifstream fp(_snames[i]);
    if(fp.fail()) {
        exec_got[0][cont_to_cluster[i]] = 0;
    } 
    else {
      std::string tmpStr;
      std::getline(fp, tmpStr);
      std::stringstream linestream(tmpStr);
      float exec, exec2, input;
      linestream >> exec >> exec2 >> input;
      float queue_metric = (exec2-exec)/(exec);
      float _exec_metric = (exec)/(exec_desired[0][i]);
      exec_got[0][cont_to_cluster[i]] += exec;
      exec_metric[0][i] = _exec_metric;
 
      if(queue_metric > 10) {
	queue_slowdown[cont_to_cluster[i]] = true;
        flow_slowdown[0] = true;
      }
      if(_exec_metric > 1.05) {
	exec_slowdown[cont_to_cluster[i]] = true;
        flow_slowdown[0] = true;
      }
      if(queue_metric > 1) {
	queue_okay[cont_to_cluster[i]] = false; 
        flow_okay[0] = false;
      }
      if(_exec_metric > 0.9 || _exec_metric == 0) {
	exec_okay[cont_to_cluster[i]] = false;
      }
    }
  }
}

//=========================================================================================================================================================//
/*
 * Basic upscaling and downscaling functions, does not include the actual controller algo to upscale/downscale
 */
//=========================================================================================================================================================//
inline void update_freq(int i, int step) {
    if(step < 0)
	return;
    cluster_freqStep[i] = step;

    // Write to freq files.
    for(int c=0; c<CORES; c++) {
      if(core_state[c] == i) {
        FILE* fp = fopen(_fnames[c].c_str(), "w");
        if(fp == nullptr) {
            std::cout << "Could not open freq file with name " << _fnames[c] << "\n";
            exit(-1);
        }
        fprintf(fp, "%i", FREQ_ARRAY[step]);
        fflush(fp);
        fclose(fp);
      }
    }
}

inline std::string get_cpu_list(int i, bool isBg = false) {
    std::string str = "";
    bool first = true;
    if(isBg) {
        for(int c=0; c<HALF*2; c++) {
	  if(core_state[c] == -1) {
	    if(first) {
	      str += std::to_string(c);
              first = false;
	    } else {
              str += "," + std::to_string(c);
	    }
	  }
	}
    } else {
	int owner = cont_to_cluster[i];
        for(int c=0; c<HALF*2; c++) {
	  if(core_state[c] == owner) {
	    if(first) {
	      str += std::to_string(c);
              first = false;
	    } else {
              str += "," + std::to_string(c);
	    }
	  }
	}
    }
    return str;
} 

void realloc_core(int core) {
  for(int i=0; i<CORES; i++) {
    if(core_state[i] == -1) {
      // Found idle core, reallocate.
      int owner = core_state[core];
      changed_core[owner] = 1;
      core_state[i] = owner;
      core_state[i+HALF] = owner;

      core_state[core] = -1;
      core_state[core+HALF] = -1;
      return;
    }
  }
}

void add_core(int owner) {
  for(int i=cidx; i<CORES; i++) {
    if(core_state[i] == -1) {
      // Found idle core, reallocate.
      changed_core[owner] = 1;
      core_state[i] = owner;
      core_state[i+HALF] = owner;
      cidx++;
      cpus[owner]++;
      return;
    }
  }
  for(int i=0; i<cidx; i++) {
    if(core_state[i] == -1) {
      // Found idle core, reallocate.
      changed_core[owner] = 1;
      core_state[i] = owner;
      core_state[i+HALF] = owner;
      cidx++;
      cpus[owner]++;
      return;
    }
  }
}

void yield_core(int owner) {
  for(int i=cidx; i<CORES; i++) {
    if(core_state[i] == owner) {
      // Found idle core, reallocate.
      changed_core[owner] = 1;
      core_state[i] = -1;
      core_state[i+HALF] = -1;
      cpus[owner]--;
      cidx++;
      return;
    }
  }
  for(int i=0; i<cidx; i++) {
    if(core_state[i] == owner) {
      // Found idle core, reallocate.
      changed_core[owner] = 1;
      core_state[i] = -1;
      core_state[i+HALF] = -1;
      cidx++;
      cpus[owner]--;
      return;
    }
  }
}
void alloc_core(int core, int owner) {
  changed_core[owner] = 1;
  core_state[core] = owner;
  core_state[core+HALF] = owner;
  cpus[owner]++;
}

void remove_core(int core, int owner) {
  changed_core[owner] = 1;
  core_state[core] = -1;
  core_state[core+HALF] = -1;
  cpus[owner]--;
}

//=========================================================================================================================================================//
/*
 * Actual controller logic. Implements when and what to upscale/downscale.
 */
//=========================================================================================================================================================//
void update_sens_fn() {
  for(int i=0; i<FLOWS; i++) {
    for(int j=0; j<num_clusters; j++) {
      exec_time[i][j][cpus[j]] = 0.5*exec_time[i][j][cpus[j]] + 0.5*exec_got[i][j];
     }
  }
}

void remove_insensitive() {
  for(int i=0; i<FLOWS; i++) {
    if(!flow_okay[i])
      continue;
    // Remove cores from the flows with low sensitivity.
    for(int j=0; j<num_clusters; j++) {
      if(cpus[j] > 1) {
        if(exec_time[i][j][cpus[j]-1] > 0) {
          float sens = 1-exec_time[i][j][cpus[j]]/exec_time[i][j][cpus[j]-1];
          if(sens < 0.05)
            yield_core(j);
        }
      }
    }
  }
}

void upscale_slow(int ctr, int flow, bool downstream_only, bool container_only) {
  if(container_only) {
    // Only upscale this container.
    if(cluster_freqStep[cont_to_cluster[ctr]] != MAXSTEP) {
      update_freq(cont_to_cluster[ctr], MAXSTEP);
    } else {
      add_core(cont_to_cluster[ctr]);
    }
  }

  if(downstream_only) {
    // Only upscale downstream containers. Try freq first, if not, add core.
    for(int i=0; i<3; i++) {
      if(downstream[flow][i] >= 0)
	break;
      int cluster = cont_to_cluster[downstream[flow][i]];
      if(cluster_freqStep[cluster] != MAXSTEP) 
        update_freq(cluster, MAXSTEP);
      else 
        add_core(cluster);
    }
  }
}

void downscale(int flow) {
  // For downscale, start with the container that has the smallest expected exec time
  // First downscale frequency for all containers in the flow, then move on to reducing cores.
  // Keep variable around to do this check.
  for(auto it : container_flow_array[flow]) {
    int cluster = cont_to_cluster[it];
    if(cluster_freqStep[cluster] > FREQ_STEP_DES) {
      update_freq(cluster, FREQ_STEP_DES);
      break;
    }
    else if(cpus[cluster] > 1) {
      yield_core(cluster);
      break;
    }
  }
}

/*
 * After getting the fast and slow path info, decide what to upscale.
 */
void decide() {
  read_stats_file();
  struct timespec st;

  bool upscaled_any = false;
  // In each case, identify the edge that should be chosen for the downscaling. This should be calculated in the detect functions.
  // Only do things based on fp_issue if sl_issue and st_issue are not pressent
  for(int i=0; i<FLOWS; i++) {
    if(flow_okay[i]) {
      clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &st);
      float passed = (st.tv_sec-last_upscale_time[i].tv_sec)*1000000 + ((float)st.tv_nsec-last_upscale_time[i].tv_nsec)/1000.0; 
      if(passed > 50) {  // Deallocate every 50 usec.
        downscale(i);
      }
    }
  }

  for(int i=0; i<FLOWS; i++) {
    if(flow_slowdown[i]) {
      // First check for queue slowdown/
      clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &last_upscale_time[i]);
      if(queue_slowdown[i]) { 
        int edge = node_to_edge(st_loc[i], i);
        downstream_on_all_node(edge, i);
        upscale_slow(-1, i, true, false);
        upscaled_any = false;
      }
    }
    else if(exec_slowdown[i]) {
      // Check for exec_slowdown per service, upscale starting from highest violation.
      for(int j=0; j<container_flow_array[i].size(); j++) {
        float max = -1;
        int max_idx = -1;
        for(auto it: container_flow_array[i]) {
          if(exec_metric[i][it] > 1.05 && max <= exec_metric[i][it] && !upscaled[it]) {
	    max = exec_metric[i][it];
            max_idx = it;
          }
        }
        if(max_idx != -1) {
	  upscale_slow(max_idx, -1, false, true);
          upscaled[max_idx] = true;
        } else
	  break;
      }
    }
  }
}

//=========================================================================================================================================================//
/*
 * Initialization of system state
 */
//=========================================================================================================================================================//
void init_cluster() {
  // Clustering for chain.
  core_state[0] = 0; core_state[HALF+0] = 0;
  core_state[1] = 1; core_state[HALF+1] = 1;
  core_state[2] = 2; core_state[HALF+2] = 2;
  core_state[3] = 3; core_state[HALF+3] = 3;
  core_state[4] = 4; core_state[HALF+4] = 4;
  core_state[5] = 5; core_state[HALF+5] = 5;
  cpus[0] = cpus[1] = cpus[2] = cpus[3] = cpus[4] = cpus[5] = 1;
  
  int i=6;

  for(; i<CORES; i++) 
    core_state[i] = -1;
  for(; i<HALF; i++) 
    core_state[i] = -2;

  i = HALF+6;
  for(; i<HALF+CORES; i++) 
    core_state[i] = -1;
  for(; i<HALF*2; i++) 
    core_state[i] = -2;
}

void do_all_allocations() {
    // Enforce CPU and core allocations for all containers.
    // Do docker update.
    for(int i=0; i<CONTS; i++) {
        if(place[i] == MY_NODE_ID) {
            std::string st = ("docker update --cpuset-cpus " + get_cpu_list(i) + " " + container_name_array[i]);
            system(st.c_str());
        }
    }
}

//=========================================================================================================================================================//
/*
 * Configuration and initialization functions
 */
//=========================================================================================================================================================//
void read_flow_file() {
  // Each line looks like edge node node node ...
  std::ifstream file("/home/cc/paper_setup/config/config_social_flow");
  std::string line;
  uint32_t edge, node;
  
  for(int i=0; i<FLOWS; i++) {
    std::getline(file, line);
    int num_edges = std::stoi(line);
    for(int j=0; j<num_edges; j++) {
      std::vector<int> down;
      std::getline(file, line);
      std::stringstream linestream(line);
      linestream >> edge;
      while(linestream >> node) {
        down.push_back(node);
        bool found = false;
        for(int it=0; it < container_flow_array[i].size(); it++) {
          if(node == container_flow_array[i][it])
            found = true;
        }
        if(!found)
          container_flow_array[i].push_back(node);
      }
      downstream_conts[i][edge] = down;
    }
  }
}

void read_config_file() {
  // Fills in container_name_array, downstream_conts, place
  // Read input file and populate inputs. Each line has format {id name target_latency cluster_id}
  std::ifstream file("/home/cc/paper_setup/config/config_social_cluster");
  std::string line;
  std::getline(file, line);
  num_containers = std::stoi(line);
  std::getline(file, line);
  num_clusters = std::stoi(line);

  // Read the input file and set up things.
  for(int i=0; i<num_containers; i++) {
    std::getline(file, line);
    std::stringstream linestream(line);
    std::string data;
    
    int id;
    linestream >> id >> container_name_array[i];
    for(int j=0; j<FLOWS; j++) {
      linestream >> exec_desired[j][i] >> input_desired[j][i];
    }
    linestream >> cluster_id[i] >> place[i];
    cluster_to_conts[cluster_id[i]].push_back(i);
  }
}

void file_setup() {
  std::string file1 = "/home/cc/paper_setup/shared/";
  for(int i=0; i<num_containers; i++) {
    _snames[i] = file1 + container_name_array[i];
  }
}

void init_setup() {
  read_config_file();
  read_flow_file();
  
  // Set up the communication pipes.
  file_setup();

  // Set up the frequency files.
  for(int i=0; i<CONTS; i++) {
    _fnames[i] = cpufreq1+std::to_string(i)+cpufreq2;
  }

  // Initialize the other arrays.
  for(int i=0; i<FLOWS; i++) {
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &last_upscale_time[i]);
    for(int ii=0; ii<CONTS; ii++) {
      for(int iii=0; iii<CORES; iii++) {
        exec_time[i][ii][iii] = 0;
      }
    }
  }
}

//=========================================================================================================================================================//
/*
 * Main loop
 */
//=========================================================================================================================================================//
int main() {
  struct timespec ts1, ts2;
    int i=0;
    float passed = 0;

    init_setup();
    do_all_allocations();
    sleep(10);

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts1);
  do {
        i++;
        decide();
        if(i == 10) {
            i = 0;
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts2);
            passed = (ts2.tv_sec-ts1.tv_sec)*1000 + ((float)ts2.tv_nsec-ts1.tv_nsec)/1000000.0; 
        }
  } while(passed < 40000);
}
