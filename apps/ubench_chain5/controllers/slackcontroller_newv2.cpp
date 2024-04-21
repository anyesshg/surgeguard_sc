#define MY_NODE_ID 0
#define HALF 32

#define CORES 24
#define CONTS 32
#define FLOWS 1
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
bool exec_slowdown[CONTS]; bool exec_okay[CONTS];
bool queue_slowdown[CONTS]; bool queue_okay[CONTS];
int score[CONTS];

float tsc[CONTS];
float this_tsc;

bool queue_issue_found, input_issue_found;

std::map<int, std::vector<int>> downstream_conts[FLOWS];
 
// Variables calculated in read_stats()
bool flow_slowdown[FLOWS]; bool flow_okay[FLOWS];

// Extra variables needed for deciding what to upscale
int cpus[CONTS];
int cidx = 0;
bool upscaled[CONTS];
int cluster_freqStep[CONTS];    // cluster to freqStep

// Setup and node state
int num_containers, num_flows;

// Variables set in initialize()
std::vector<int> container_flow_array[FLOWS];            // Array of all active containers in any given flow. Indexed by flowID.
float exec_desired[FLOWS][CONTS], input_desired[FLOWS][CONTS];    // Array of desired input and exec time for each container, for each flow.
float exec_avg[FLOWS][CONTS][HALF];	// Used for sens part

int downstream[FLOWS][3];
struct timespec last_upscale_time[FLOWS];

// File names for the various things needed
std::string _cname[CONTS];      // Container name
std::string _snames[CONTS];     // Stats file names
std::string _fnames[CONTS];     // Frequency file names
int place[CONTS];

// Resource change
std::string stats1 = "/home/cc/paper_setup/shared/";
std::string cpufreq1 = "/sys/devices/system/cpu/cpu";
std::string cpufreq2 = "/cpufreq/scaling_setspeed";
uint32_t FREQ_ARRAY[24] = {800000, 900000, 1000000, 1100000, 1200000, 1300000, 1400000, 1500000, 1600000, 1700000, 1800000, 1900000, 2000000, 2100000, 2200000, 2300000, 2400000, 2500000, 2600000, 2700000, 2800000, 2900000, 3000000, 3100000};

// Things for defining whether resource changes have occured, these are used by the actual resource update fns
bool changed_core[CONTS];
int st_loc_max[FLOWS], st_loc_min[FLOWS];

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
            case 0: return 0;
            case 1: return 1;
            case 2: return 2;
            case 3: return 3;
            case 4: return 4;
            case 5: return 5;
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

void init_cluster() {
  // Clustering for chain.
  core_state[0] = 0; core_state[HALF+0] = 0;
  core_state[1] = 0; core_state[HALF+1] = 0;
  core_state[2] = 0; core_state[HALF+2] = 0;
  
  core_state[3] = 1; core_state[HALF+3] = 1;
  core_state[4] = 1; core_state[HALF+4] = 1;
  core_state[5] = 1; core_state[HALF+5] = 1;
  
  core_state[6] = 2; core_state[HALF+6] = 2;
  core_state[7] = 2; core_state[HALF+7] = 2;
  core_state[8] = 2; core_state[HALF+8] = 2;
  
  core_state[9] = 3; core_state[HALF+9] = 3;
  core_state[10] = 3; core_state[HALF+10] = 3;
  core_state[11] = 3; core_state[HALF+11] = 3;
  
  core_state[12] = 4; core_state[HALF+12] = 4;
  core_state[13] = 4; core_state[HALF+13] = 4;
  core_state[14] = 4; core_state[HALF+14] = 4;
  
  core_state[15] = 5; core_state[HALF+15] = 5;
  core_state[16] = 5; core_state[HALF+16] = 5;
  core_state[17] = 5; core_state[HALF+17] = 5;
  cpus[0] = cpus[1] = cpus[2] = cpus[3] = cpus[4] = cpus[5] = 3;
  
  for(int i=0; i<num_containers; i++)
    changed_core[i] = true;
  
  int i=18;

  for(; i<CORES; i++) 
    core_state[i] = -1;
  for(; i<HALF; i++) 
    core_state[i] = -2;

  i = HALF+18;
  for(; i<HALF+CORES; i++) 
    core_state[i] = -1;
  for(; i<HALF*2; i++) 
    core_state[i] = -2;
}

void read_stats_file() {
  for(int i=0; i<num_containers; i++) {
    queue_slowdown[i] = exec_slowdown[i] = false;
    queue_okay[i] = exec_okay[i] = true;
    score[i] = 0;
  }
  for(int i=0; i<FLOWS; i++) {
    flow_slowdown[i] = false;
    flow_okay[i] = true;
  }
  
  queue_issue_found = false;
  input_issue_found = false;

  // Read all the stats files.
  for(int i=0; i<num_containers; i++) {
    std::ifstream fp(_snames[i]);
    if(fp.fail()) {
        exec_okay[i] = false;
	queue_okay[i] = false;
    } 
    else {
      std::string tmpStr;
      std::getline(fp, tmpStr);
      if(tmpStr.size() < 5) {
	// Not reading real values yet, just set exec_okay
	exec_okay[i] = false;
	continue;
      }
      std::stringstream linestream(tmpStr);
      float exec, exec2, input;
      linestream >> exec >> exec2 >> input;

      float in_metric = input/input_desired[0][i];
      float queue_metric = (exec2)/(exec);
      float _exec_metric = (exec)/(exec_desired[0][i]);
      
      if(exec_avg[0][i][cpus[i]] != -1)
        exec_avg[0][i][cpus[i]] = 0.85*exec_avg[0][i][cpus[i]] + 0.15*exec;
      else
	exec_avg[0][i][cpus[i]] = exec;

      if(!input_issue_found) {
        // Alternate metric that looks at scene at last container only.
        if(i == 4) {
          if(exec + input > 1.5*(exec_desired[0][i]+input_desired[0][i])) {
            flow_slowdown[0] = true;
	    for(int j=0; j<num_containers; j++) {
	      score[j]++;
	    }
            input_issue_found = true;
          }
        }
      }

      if(!queue_issue_found) {
        if(queue_metric > 4) {
          flow_slowdown[0] = true;
	  for(int j=1; j<=3; j++) {
	    if(i+j < num_containers)
	      score[i+j]++;
	  }
	  queue_issue_found = true;
	}
      }
      if((i != 0 && _exec_metric > 1.1) || (i == 0 && _exec_metric > 4)) {
	exec_slowdown[i] = true;
        flow_slowdown[0] = true;
	score[i]++;
        //if(i == 0)
	//  printf("%f %f\n", exec, exec_desired[0][0]);
      }
      if(queue_metric > 1.5) {
	queue_okay[i] = false; 
        flow_okay[0] = false;
      }
      if(_exec_metric > 0.9 || _exec_metric == 0) {
	exec_okay[i] = false;
      }
    }
    fp.close();
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
    for(int c=0; c<HALF; c++) {
      if(core_state[c] == i) {
        FILE* fp = fopen(_fnames[c].c_str(), "w");
        if(fp == nullptr) {
            std::cout << "Could not open freq file with name " << c << _fnames[c] << "\n";
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
	int owner = i;
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
      tsc[owner] = this_tsc;
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
      tsc[owner] = this_tsc;
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
      tsc[owner] = this_tsc;
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
      tsc[owner] = this_tsc;
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
  tsc[owner] = this_tsc;
  changed_core[owner] = 1;
  core_state[core] = owner;
  core_state[core+HALF] = owner;
  cpus[owner]++;
}

void remove_core(int core, int owner) {
  tsc[owner] = this_tsc;
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
void remove_insensitive() {
  for(int i=0; i<FLOWS; i++) {
    if(!flow_okay[i])
      continue;
    // Remove cores from the flows with low sensitivity.
    for(int j=0; j<num_containers; j++) {
      if(cpus[j] > 1 && this_tsc - tsc[j] > 200) {
        if(exec_avg[i][j][cpus[j]-1] > 0) {
          float sens = 1-exec_avg[i][j][cpus[j]]/exec_avg[i][j][cpus[j]-1];
          if(sens < 0.02 && sens > -0.05)
            yield_core(j);
        }
      }
    }
  }
}

//=========================================================================================================================================================//
/*
 * Initialization of system state
 */
//=========================================================================================================================================================//

void do_all_allocations() {
    // Enforce core allocations
    // Each cluster must have at least one core to avoid pathological cond
    for(int i=0; i<num_containers; i++) {
        if(changed_core[i]) {
            std::string str = ("docker update --cpuset-cpus " + get_cpu_list(i) + " ");
 	    std::cout << str << _cname[i] << "\n";
            system((str + _cname[i]).c_str());
        }
    }
}


//=========================================================================================================================================================//
/*
 * Controller version 2
 */
//=========================================================================================================================================================//
void upscale2(int flow) {
  while(1){
    // Upscale starting from the highest score.
    int maxScore = -1;
    float maxExec = -1;
    int idx = -1;

    for(int i=0; i<num_containers; i++) {
      if(score[i] > maxScore && !upscaled[i]) {
        maxExec = exec_desired[0][i];
        maxScore = score[i];
        idx = i;
      }
      if(score[i] == maxScore && !upscaled[i] && maxExec < exec_desired[0][i]) {
        maxExec = exec_desired[0][i];
        idx = i;
      }
    }
    if(idx == -1) 
      break;
    if(maxScore == 1 && queue_issue_found) 	// Do not upscale these
      break; 
    upscaled[idx] = true;
    if(cluster_freqStep[idx] != MAXSTEP) {
      update_freq(idx, MAXSTEP);
    } else {
      add_core(idx);
    }
  }
}

void downscale2(int flow) {
  // For downscale, start with the container that has the smallest expected exec time
  // First downscale frequency for all containers in the flow, then move on to reducing cores.
  // Keep variable around to do this check.
  bool ds = false;
  for(auto it : container_flow_array[flow]) {
    if(cluster_freqStep[it] > FREQ_STEP_DES) {
      update_freq(it, cluster_freqStep[it]-2);
      ds = true;
    }
  }
  if(ds)  return;
  for(auto it: container_flow_array[flow]) {
    if(cpus[it] > 1) {
      yield_core(it);
      break;
    }
  }
}

void decide2() {
  read_stats_file();
  struct timespec st;

  // In each case, identify the edge that should be chosen for the downscaling. This should be calculated in the detect functions.
  // Only do things based on fp_issue if sl_issue and st_issue are not pressent
  for(int i=0; i<FLOWS; i++) {
    if(flow_okay[i]) {
      clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &st);
      float passed = (st.tv_sec-last_upscale_time[i].tv_sec)*1000000 + ((float)st.tv_nsec-last_upscale_time[i].tv_nsec)/1000.0; 
      if(passed > 50) {  // Deallocate every 50 usec.
        downscale2(i);
      }
    }
  }

  for(int i=0; i<FLOWS; i++) { 
    if(flow_slowdown[i])
      upscale2(i);
  }
}

//=========================================================================================================================================================//
/*
 * Configuration and initialization functions
 */
//=========================================================================================================================================================//
void read_flow_file() {
  // Each line looks like edge node node node ...
  std::ifstream file("/home/cc/paper_setup/config/config_ping_flow");
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
  // Fills in _cname, downstream_conts, place
  // Read input file and populate inputs. Each line has format {id name target_latency cluster_id}
  std::ifstream file("/home/cc/paper_setup/config/config_ping_cluster");
  std::string line;
  std::getline(file, line);
  num_containers = std::stoi(line);
  std::getline(file, line);
  num_containers = std::stoi(line);

  // Read the input file and set up things.
  for(int i=0; i<num_containers; i++) {
    std::getline(file, line);
    std::stringstream linestream(line);
    std::string data;
    
    int id, blah1;
    linestream >> id >> _cname[i];
    for(int j=0; j<FLOWS; j++) {
      linestream >> exec_desired[j][i] >> input_desired[j][i];
    }
    linestream >> blah1 >> place[i];
  }
}

void file_setup() {
  std::string file1 = "/home/cc/paper_setup/shared/";
  for(int i=0; i<num_containers; i++) {
    _snames[i] = file1 + _cname[i];
  }
}

void init_setup() {
  read_config_file();
  read_flow_file();
  init_cluster();
 
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
        exec_avg[i][ii][iii] = -1;
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
    int core_sum = 0;
    float full_sum = 0;
    float full_time = 0;
    struct timespec ts1, ts2, ts3;
    int i=0;

    float warmup = 25000;
    bool warmed = false;

    init_setup();
    do_all_allocations();
    sleep(5);

    clock_gettime(CLOCK_REALTIME, &ts1);
    do {
       clock_gettime(CLOCK_REALTIME, &ts2);
       this_tsc = (ts2.tv_sec*1000000 + (float)ts2.tv_nsec/1000.0);
	i++;
       for(int ii=0; ii<num_containers; ii++) {
           changed_core[ii] = false;
           upscaled[ii] = false;
       }
       decide2();
       do_all_allocations();
       usleep(100);

       if(i == 10) {
	 i = 0;
	 remove_insensitive();
       }

       core_sum = 0;
       for(int ii=0; ii<HALF*2; ii++) {
	   if(core_state[ii] >= 0)
	    core_sum++;
       }
	clock_gettime(CLOCK_REALTIME, &ts3);
	if ((ts3.tv_sec-ts1.tv_sec)*1000 + (ts3.tv_nsec-ts1.tv_nsec)/1000000.0 < warmup) {
	  // warmup period
	  ;
	} else {
	  if(!warmed) {
	    warmed = true;
	    init_cluster();
	    do_all_allocations();
	  }
	  full_sum += core_sum*((ts3.tv_sec-ts2.tv_sec)*1000000 + (ts3.tv_nsec-ts2.tv_nsec)/1000.0);
	  full_time += 1*((ts3.tv_sec-ts2.tv_sec)*1000000 + (ts3.tv_nsec-ts2.tv_nsec)/1000.0);
	}
   } while (((ts3.tv_sec-ts1.tv_sec)*1000+(ts3.tv_nsec-ts1.tv_nsec)/1000000.0) < 60000);  
    printf("CoreUsage (us-cores): %fi, avg cores used: %f\n", full_sum, full_sum/full_time);
    printf("============Sensitivity=============\n");

  for(int i=0; i<FLOWS; i++) {
    for(int j=0; j<num_containers; j++) {
      for(int k=1; k<10; k++) {
        float sens = exec_avg[i][j][k];
        printf("%f ", sens);
      }
      printf("\n");
    }
  }
    return -1;
}
