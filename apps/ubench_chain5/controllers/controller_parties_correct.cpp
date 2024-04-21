#define CORES 24
#define HALF 32
#define CONTS 32
#define FLOWS 1
#define WAYS 7
#define EMPTYWAY 10
#define FREQ_STOP_PREF 15
#define FREQ_STOP_INC 18
#define FREQ_STEP_DES 8
#define MAXSTEP 23

#include <vector>
#include <iostream>
#include <unistd.h>
#include <string>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <time.h>
#include <cmath>
#include <cassert>

// This file has an implementation of caladan algorithm
int core_state[HALF*2];			
bool exec_slowdown[CONTS];
bool exec_okay[CONTS];
int cpus[CONTS];
int cidx = 0;
bool upscaled[CONTS];

// Setup and node state
int num_containers;
int num_flows;

// ContainerState should only contain the latency values.
struct containerState {
    // Desired target
    float input_desired;
    float exec_desired;
    // Current execution latency
    float avg_exec;
};
containerState _ctr[CONTS];

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

// Temporary variables
float maxViolation[CONTS];

// variables for cluster state
//std::vector<int> clusters[CONTS];   // cluster to containers
int cont_to_cluster[CONTS];         // container to cluster
int num_clusters;                   
//std::vector<int> cluster_to_cpus[CONTS];    // cluster to cores allocated
//int cluster_to_llc[CONTS];                  // cluster to llc allocated
int cluster_freqStep[CONTS];                // cluster to freqStep
//bool clusterEmpty[CONTS];

/*
 * Functions for starting off with a predefined parititoning.
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

/* 
 * Functions for setting the allocations to the files/counters.
*/
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

void do_all_allocations() {
    // Enforce core allocations
    // Each cluster must have at least one core to avoid pathological cond
    for(int i=0; i<num_clusters; i++) {
        if(changed_core[i]) {
            std::string str = ("docker update --cpuset-cpus " + get_cpu_list(i) + " ");
            for(int it=0; it<num_containers; it++) {
		if(cont_to_cluster[it] == i) {
		  std::cout << str << _cname[it] << "\n";
                  system((str + _cname[it]).c_str());
		}
	    }
        }
    }
}

/*
 * Functions to update internal variables used for state assignment. do_all_allocations() depends on the output of this
 */
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

inline void update_freq_core(int i, int step) {
    if(step < 0)
	return;
    
    // Write to freq files.
    FILE* fp = fopen(_fnames[i].c_str(), "w");
    if(fp == nullptr) {
        std::cout << "Could not open freq file with name " << _fnames[i] << "\n";
        exit(-1);
    }
    fprintf(fp, "%i", FREQ_ARRAY[step]);
    fflush(fp);
    fclose(fp);
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

/*
 * Functions which read system state and provide inputs for core inc./dec. functions
 */
void read_stats_file() {
  float blah1, blah2;
  // Expected input: avg_exec long_input short_input requests
  // Expected input for multi-flow: avg_exec <long_input short_input requests> <...> <...> ...
  for(int i=0; i<num_clusters; i++) {
    exec_slowdown[i] = false;
    exec_okay[i] = true;
    maxViolation[i] = 0;
  }
  
  // Read all the stats files.
  for(int i=0; i<num_containers-1; i++) {
    std::ifstream fp(_snames[i]);
    if(fp.fail()) {
        _ctr[i].avg_exec = -1;
	//assert(i == cont_to_cluster[i]);
        exec_okay[cont_to_cluster[i]] = false;
    } 
    else {
      std::string tmpStr;
      std::getline(fp, tmpStr);
      if(tmpStr.size() < 5) {
	// Not reading real values yet, just set exec_okay
	exec_okay[cont_to_cluster[i]] = false;
	continue;
      }
      std::stringstream linestream(tmpStr);
      linestream >> blah1 >> _ctr[i].avg_exec >> blah2;
      float exec_metric = (_ctr[i].avg_exec)/(_ctr[i].exec_desired);
      int owner = cont_to_cluster[i];

      if(exec_metric > 1.2)
	exec_slowdown[owner] = true;
      if(exec_metric > 0.9)
	exec_okay[owner] = false; 
      if(exec_metric > maxViolation[owner])
	maxViolation[owner] = exec_metric;
      fp.close();
    }
  }

  for(int i=0; i<num_clusters; i++) {
    if(exec_slowdown[i] && exec_okay[i])
      exec_okay[i] = false;
  }
}

/*
 * Calculations for the core algorithm.
 */
void increase_resources() {
    // This functions has to be changed between versions
    float max = -1;
    int maxIdx = -1;

    for(int i=0; i<num_clusters; i++) {
        upscaled[i] = false;
    }

    while(1) {
        max = -1;
        for(int i=0; i<num_clusters; i++) {
	  if(exec_slowdown[i] && !upscaled[i]) {
	    if(maxViolation[i] > max) {
              max = maxViolation[i];
              maxIdx = i;
            }
          }
        }
        if(max < 0) // Nothing to upscale any more
          return;
        
        int owner = maxIdx;
        upscaled[owner] = true;
	float sel = (float)rand()/(float)RAND_MAX;
	if(sel < 0.5 && cluster_freqStep[owner] < MAXSTEP) {
	  // Upscale frequency by 1 pt.
          update_freq(owner, MAXSTEP);
        }
	else {
          add_core(owner);
        } 
    } 
    return;
}

void recover_resources() {
    for(int i=0; i<num_clusters; i++) {
      if(exec_okay[i]) {
       float select = (float)rand()/(float)RAND_MAX;
       bool fallthrough = false;
       if(select < 0.5) {
         if(cluster_freqStep[i] > FREQ_STEP_DES)  
             update_freq(i, cluster_freqStep[i] -1);
         else
             fallthrough = true;
       } 
       if((select >= 0.5) || fallthrough) {
         // cores
         if(cpus[i] > 1)
          yield_core(i);
       } 
      }
    }
}

/*
 * Initialization and profile update functions
 */
void initialize() {
    for(int i=0; i<CONTS; i++)
        upscaled[i] = false;

    for(int i=0; i<CONTS; i++) {
        _fnames[i] = cpufreq1+std::to_string(i)+cpufreq2;
    }


    // Read input file and populate inputs. Each line has format {id name target_latency cluster_id}
    std::ifstream file("/home/cc/paper_setup/config/config_ping_cluster2");
    std::string line;
    std::getline(file, line);
    num_containers = std::stoi(line);
    std::getline(file, line);
    num_clusters = std::stoi(line);

    init_cluster();
    
    // Set frequency of everything to 8.
    for(int i=0; i<num_clusters; i++) {
        update_freq(i, FREQ_STEP_DES);
        changed_core[i] = true;
    }

    // Read the input file and set up things.
    for(int i=0; i<num_containers; i++) {
        std::getline(file, line);
        std::stringstream linestream(line);
        std::string data;
        
        int id;
	int clusterID;
	std::string name;

        linestream >> id >> name;
	if(name == "nginx-thrift")
	    continue;

	_cname[i] = name;
	for(int j=0; j<1; j++) {
	    linestream >> _ctr[i].exec_desired >> _ctr[i].input_desired;
	}
	linestream >> clusterID >> place[i];
	cont_to_cluster[i] = clusterID;
        _ctr[i].avg_exec = _ctr[i].exec_desired;
        _snames[i] = stats1+name;
    }
    do_all_allocations();
}

void decide() {
  read_stats_file();
  recover_resources();
  increase_resources();
  do_all_allocations();
}

/*
 * Main() function
 */
int main(int argc, char* argv[]) {
    int core_sum = 0;
    float full_sum = 0;
    float full_time = 0;

    for(int i=0; i<CONTS; i++) {
      cpus[i] = 0;
    }
    initialize();
    sleep(10);
    int i=0;
    //FILE* fp0 = fopen("resources_core", "w");
    //FILE* fp1 = fopen("resources_cache", "w");
    //FILE* fp2 = fopen("resources_freq", "w");
    struct timespec ts1, ts2, ts3;
    clock_gettime(CLOCK_REALTIME, &ts1);
   
    // Start after 10s and then poll for 25s.
    do {
       clock_gettime(CLOCK_REALTIME, &ts2);
	i++;
       for(int ii=0; ii<num_clusters; ii++) {
           changed_core[ii] = false;
       }
       decide();
       usleep(100);

	core_sum = 0;
       for(int ii=0; ii<HALF*2; ii++) {
	   if(core_state[ii] >= 0)
	    core_sum++;
       }
	clock_gettime(CLOCK_REALTIME, &ts3);
	full_sum += core_sum*((ts3.tv_sec-ts2.tv_sec)*1000000 + (ts3.tv_nsec-ts2.tv_nsec)/1000.0);
	full_time += 1*((ts3.tv_sec-ts2.tv_sec)*1000000 + (ts3.tv_nsec-ts2.tv_nsec)/1000.0);

   } while (((ts3.tv_sec-ts1.tv_sec)*1000+(ts3.tv_nsec-ts1.tv_nsec)/1000000.0) < 60000);  

	//fprintf(fp0, "%d %d %d %d %d %d %d\n", cluster_to_cpus[0].size(), cluster_to_cpus[1].size(), cluster_to_cpus[2].size(), cluster_to_cpus[3].size(), cluster_to_cpus[4].size(), cluster_to_cpus[5].size(), bgCores.size());
	//fprintf(fp1, "%d %d %d %d %d %d %d\n", cluster_to_llc[0], cluster_to_llc[1], cluster_to_llc[2], cluster_to_llc[3], cluster_to_llc[4], cluster_to_llc[5], bgLLC);
	//fprintf(fp2, "%d %d %d %d %d %d\n", cluster_freqStep[0], cluster_freqStep[1], cluster_freqStep[2], cluster_freqStep[3], cluster_freqStep[4], cluster_freqStep[5]);
	//fflush(fp0); fflush(fp1); fflush(fp2);
    printf("CoreUsage (us-cores): %fi, avg cores used: %f\n", full_sum, full_sum/full_time);
    return -1;
}
