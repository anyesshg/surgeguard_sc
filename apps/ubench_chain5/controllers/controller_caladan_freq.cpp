#define CORES 24
#define HALF 32
#define CONTS 32
#define FLOWS 1
#define WAYS 7
#define EMPTYWAY 10
#define FREQ_STOP_PREF 15
#define FREQ_STOP_INC 18
#define FREQ_STEP_DES 12
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

// This file has an implementation of caladan algorithm
int core_state[HALF*2];			
uint64_t last_alloc[CONTS][HALF*2];		// Last use time
uint64_t tsc;
bool exec_slowdown[CONTS];
bool queue_slowdown[CONTS];
bool queue_okay[CONTS];
int cpus[CONTS];

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
    float avg_exec2;
    float avg_input;
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

// variables for cluster state
int cont_to_cluster[CONTS];         // container to cluster
int num_clusters;                   
int cluster_freqStep[CONTS];                // cluster to freqStep

/*
 * Functions for starting off with a predefined parititoning.
 */
void init_cluster() {
  // Clustering for chain.
  core_state[0] = 0; core_state[HALF+0] = 0;
  core_state[1] = 1; core_state[HALF+1] = 1;
  core_state[2] = 2; core_state[HALF+2] = 2;
  core_state[3] = 3; core_state[HALF+3] = 3;
  core_state[4] = 4; core_state[HALF+4] = 4;
  core_state[5] = 5; core_state[HALF+5] = 5;
  cpus[0] = cpus[1] = cpus[2] = cpus[3] = cpus[4] = cpus[5] = 2;
  
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

inline void update_freq(int i, int step) {
    if(step < 0)
	return;
    cluster_freqStep[i] = step;

    // Write to freq files.
    for(int c=0; c<HALF*2; c++) {
      if(core_state[c] == i) {
	if(c < HALF) {
	  if(cluster_freqStep[core_state[c+HALF]] > step) 
	    return;
	} else {
	  if(cluster_freqStep[core_state[c-HALF]] > step) 
	    return;
	}
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

/* Caladan implementation
 */
void realloc_core(int core, int do_not_tread) {
  for(int i=0; i<CORES; i++) {
    if(core_state[i] == -1 && core_state[i+HALF] != do_not_tread) {
      // Found idle core, reallocate.
      int owner = core_state[core];
      changed_core[owner] = 1;
      core_state[i] = owner;
      core_state[core] = -1;
      last_alloc[owner][core] = tsc;  
      if(core >= HALF)
        last_alloc[owner][core-HALF] = tsc;  
      else
        last_alloc[owner][core+HALF] = tsc;  
      return;
    }
  }
  for(int i=HALF; i<HALF+CORES; i++) {
    if(core_state[i] == -1 && core_state[i-HALF] != do_not_tread) {
      // Found idle core, reallocate.
      int owner = core_state[core];
      changed_core[owner] = 1;
      core_state[i] = owner;
      core_state[core] = -1;
      last_alloc[owner][core] = tsc;  
      if(core >= HALF)
        last_alloc[owner][core-HALF] = tsc;  
      else
        last_alloc[owner][core+HALF] = tsc;  
      return;
    }
  }
}

void alloc_core(int core, int owner) {
  changed_core[owner] = 1;
  core_state[core] = owner;
  last_alloc[owner][core] = tsc;  
  if(core >= HALF)
    last_alloc[owner][core-HALF] = tsc;  
  else
    last_alloc[owner][core+HALF] = tsc;  
  cpus[owner]++;
}

void remove_core(int core, int owner) {
  changed_core[owner] = 1;
  core_state[core] = -1;
  cpus[owner]--;
}

void ht_ctlr() {
  bool free = false;
  for(int i=0; i<HALF*2; i++) {
    if((i >= CORES && i < HALF) || (i >= HALF+CORES && i < 2*HALF))
      continue;
    if(core_state[i] == 0)
      free = true;
  }
  if(!free)
    return;

  for(int i=0; i<num_clusters; i++) {
    if(exec_slowdown[i]) {
      for(int c=0; c<CORES; c++) {
        if(core_state[c] == i) {
          if(core_state[c+HALF] != i && core_state[c+HALF] != -1) {
	    // Sibling is not idle and belongs to someone else, realloc sibling proc and allocate ht to me!
	    // Once one core has been reallocated, go to next cluster.
	    realloc_core(c+HALF, i);
            goto next;
	  }
	}
      }
      for(int c=HALF; c<HALF+CORES; c++) {
        if(core_state[c] == i) {
          if(core_state[c-HALF] != i && core_state[c-HALF] != -1) {
	    // Sibling is not idle and belongs to someone else, realloc sibling proc and allocate ht to me!
	    // Once one core has been reallocated, go to next cluster.
	    realloc_core(c-HALF, i);
            goto next;
	  }
	}
      }
    }
    next: ;
  }
}

void top_level_alloc(int owner) {
  int i = (owner == -1) ? 0 : owner;
  int fin = (owner == -1) ? num_clusters : owner+1;

  for(; i<fin; i++) {
    if(owner != -1 || queue_slowdown[i]) {
      if(cluster_freqStep[i] != MAXSTEP) {
        // Upscale frequency of the cores if possible.
        update_freq(i, MAXSTEP);
        continue;
      }
    }  
    if(owner != -1 || queue_slowdown[i]) {
      bool found = false;  //We want to allocate an extra core here.
      // First search for core where both hyperthreads are idle.
      for(int c=0; c<CORES; c++) {
	if(core_state[c] == -1 && core_state[c+HALF] == -1) {
	  alloc_core(c, i);
	  found = true;
	  break;
        }
      }
      // Then search for core where one of the hyperthreads is not taken by me.
      if(!found) {
        for(int c=0; c<CORES; c++) {
	  if(core_state[c] == -1 && core_state[c+HALF] != i) {
	    alloc_core(c, i);
	    found = true;
	    break;
	  }
	}
      }
      if(!found) {
        for(int c=HALF; c<HALF+CORES; c++) {
	  if(core_state[c] == -1 && core_state[c-HALF] != i) {
	    alloc_core(c, i);
	    found = true;
	    break;
	  }
	}
      }
      // Then search for the core with has been most recently used by me.
      if(!found) {
	uint64_t max_tsc = 0;
        int max_tsc_core = -1;
	for(int c=0; c<HALF*2; c++) {
          if((c >= CORES && c < HALF) || (c >= HALF+CORES))
            continue;
	  if(core_state[c] == -1) {
	    if(last_alloc[i][c] >= max_tsc) {
	      max_tsc = last_alloc[i][c];
	      max_tsc_core = c;
	    }
	  }
	}
        alloc_core(max_tsc_core, i);
      }
    }
  }
  return;
}

void bw_ctlr() {
  // Not implementing this, this is a giant pain and also doesn't work.
  ;
  /*
  if(total_bw_shortfall) {
    float min_bw = 1.0;
    int min_bw_cluster = -1;
    for(int i=0; i<num_clusters; i++) {
      if(min_bw >= bw[i]) {
	min_bw = bw[i];
	min_bw_cluster = i;
      }
    }
    top_level_alloc(min_bw_cluster);
  }*/
}

void top_level_yield() {
  for(int i=0; i<num_clusters; i++) {
    if(queue_okay[i]) {
      if(cpus[i] == 1 || cluster_freqStep[i] > FREQ_STEP_DES) {
        update_freq(i, cluster_freqStep[i]-2);
        continue;
      } 
 
      bool found = false;
      // First remove core where I hold both apirs.
      uint64_t min_tsc = tsc;
      int min_tsc_core = -1;
      for(int c=0; c<CORES; c++) {
        if(core_state[c] == i && core_state[c+HALF] == i) {
          if(last_alloc[i][c] < min_tsc) {
            min_tsc = last_alloc[i][c];
            min_tsc_core = c;
          }
        }
      }
      if(min_tsc_core != -1) {
	found = true;
	remove_core(min_tsc_core, i);
      }
      //Then remove cores where I hold one of the hyperthreads and the other one is not idle.
      if(!found) {
        for(int c=0; c<CORES; c++) {
	  if(core_state[c] == i && core_state[c+HALF] != -1) {
	    remove_core(c, i);
	    found = true;
	    break;
	  }
	}
      }
      if(!found) {
        for(int c=HALF; c<HALF+CORES; c++) {
	  if(core_state[c] == i && core_state[c-HALF] != -1) {
	    remove_core(c, i);
	    found = true;
	    break;
	  }
	}
      }
      // Finally, we have the cores whose pair is idle. Dealloc one of these.
      if(!found) {
        for(int c=0; c<CORES; c++) {
	  if(core_state[c] == i && core_state[c+HALF] == i) {
	    remove_core(c, i);
	    found = true;
	    break;
	  }
	}
      }
    }
  }
  return;
}

/*
 * Functions which read system state and provide inputs for core inc./dec. functions
 */
void read_stats_file() {
  // Expected input: avg_exec long_input short_input requests
  // Expected input for multi-flow: avg_exec <long_input short_input requests> <...> <...> ...
  for(int i=0; i<num_clusters; i++) {
    queue_slowdown[i] = exec_slowdown[i] = false;
    queue_okay[i] = true;
  }
  
  // Read all the stats files.
  for(int i=0; i<num_containers; i++) {
    std::ifstream fp(_snames[i]);
    if(fp.fail()) {
        _ctr[i].avg_exec = -1;
        queue_okay[i] = false;
    } 
    else {
      std::string tmpStr;
      std::getline(fp, tmpStr);
      std::stringstream linestream(tmpStr);
      linestream >> _ctr[i].avg_exec >> _ctr[i].avg_exec2 >> _ctr[i].avg_input;
      float queue_metric = (_ctr[i].avg_exec2-_ctr[i].avg_exec)/(_ctr[i].avg_exec);
      float exec_metric = (_ctr[i].avg_exec)/(_ctr[i].exec_desired);

      if(queue_metric > 50)
	queue_slowdown[cont_to_cluster[i]] = true;
      if(exec_metric > 1.05)
	exec_slowdown[cont_to_cluster[i]] = true;
      if(queue_metric > 30)
	queue_okay[cont_to_cluster[i]] = false; 
    }
  }
}

void decide() {
  read_stats_file();
  top_level_yield();
  ht_ctlr();
  top_level_alloc(-1);
  do_all_allocations();
}

/*
 * Initialization and profile update functions
 */
void initialize() {
    for(int i=0; i<HALF*2; i++) {
        _fnames[i] = cpufreq1+std::to_string(i)+cpufreq2;
    }
    // Read input file and populate inputs. Each line has format {id name target_latency cluster_id}
    std::ifstream file("/home/cc/paper_setup/config/config_ping_cluster");
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

/*
 * Main() function
 */
int main(int argc, char* argv[]) {
    int core_sum = 0;
    float full_sum = 0;
    float full_time = 0;

    initialize();
    sleep(10);
    int i=0;
    struct timespec ts1, ts2, ts3;
    clock_gettime(CLOCK_REALTIME, &ts1);
   
    // Start after 10s and then poll for 25s.
    do {
       clock_gettime(CLOCK_REALTIME, &ts2);
       tsc = ts2.tv_sec*1000000000 + ts2.tv_nsec;
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

   } while (((ts3.tv_sec-ts1.tv_sec)*1000+(ts3.tv_nsec-ts1.tv_nsec)/1000000.0) < 30000);  

    printf("CoreUsage (us-cores): %fi, avg cores used: %f\n", full_sum, full_sum/full_time);
    return -1;
}
