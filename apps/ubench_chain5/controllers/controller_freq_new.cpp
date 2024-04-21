#define MY_NODE_ID 0

#define USE_KERNEL 1
#define USE_PIPES 1

#define CORES 19
#define CONTS 32
#define FLOWS 1
#define FREQ_STEP_DES 2
#define MAXSTEP 15

#define MAX_EDGES 20

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
#include <fcntl.h>
#include <sys/ioctl.h>
#include "kmod_interact.h"

/*
 * Variables for controller
 */
std::map<uint32_t, std::vector<uint32_t>> downstream_conts[FLOWS];	// map indexed by edgeID

int container_freq[CONTS];
int container_core_num[CONTS];
std::vector<int> container_cores[CONTS];

//int fp_loc_min[FLOWS], fp_loc_max[FLOWS], sl_loc[FLOWS], st_loc[FLOWS];

bool allDown[FLOWS];
uint64_t last_upscale_time[FLOWS];
uint64_t last_downscale_time[FLOWS];


//--------------------------Corrected variables go here-------------------
// Variables that have to be read from the config files
int num_containers = 0;
int num_clusters = 0;
std::string container_name_array[CONTS];		// indexed by containerID
int place[CONTS], cluster_id[CONTS];			// indexed by containerID
std::vector<uint32_t> cluster_to_conts[CONTS];

std::vector<int> container_flow_array[FLOWS];			// Array of all active containers in any given flow. Indexed by flowID.
float exec_desired[FLOWS][CONTS], input_desired[FLOWS][CONTS];	// Array of desired input and exec time for each container, for each flow.

// Variables for interacting with the kmod/bpf code.
uint32_t* shmem;
uint32_t bpf_map[FLOWS*MAX_EDGES];
uint32_t bpf_map2[FLOWS*2];

//Variables for measuring and updating frequency.
int freq_fd[CORES];
uint64_t FREQ_ARRAY[16] = {0xc00, 0xd00, 0xe00, 0xf00, 0x1100, 0x1200, 0x1300, 0x1400, 0x1500, 0x1600, 0x1700, 0x1900, 0x1a00, 0x1b00, 0x1c00, 0x2700};
//uint64_t FREQ_ARRAY[24] = {800000000, 900000000, 1000000000, 1100000000, 1200000000, 1300000000, 1400000000, 1500000000, 1600000000, 1700000000, 1800000000, 1900000000, 2000000000, 2100000000, 2200000000, 2300000000, 2400000000, 2500000000, 2600000000, 2700000000, 2800000000, 2900000000, 3000000000, 3100000000};

// Variables for interacting with the containers through pipes
//int pipes[CONTS];

// Variables for interacting with the containers through files
//const char* dataf[CONTS];

//=========================================================================================================================================================//
/*
 * Reading files/shared arrays to detect issues
 */
//=========================================================================================================================================================//
/* 
 * Functions for detecting issues on the fast and slow paths.
 */
void fast_path_detect() {
    // This will read shmem and put the results in bpf_map.	
    read_mmap(shmem, bpf_map);
}

void fast_path_detect2() {
    // This assumes that the bpf code will return the min and max violating edge for each flow.
    // This will read shmem and put the results in bpf_map.	
    read_mmap2(shmem, bpf_map2);
}
//=========================================================================================================================================================//
/*
 * Basic upscaling and downscaling functions, does not include the actual controller algo to upscale/downscale
 */
//=========================================================================================================================================================//
static inline void write_core_msr(int fd, uint64_t val) {
    pwrite(fd, &val, 8, 0x199);
    if (pwrite(fd, &val, 8, 0x199) != 8) {
        perror("pwrite");
        close(fd);
        exit(EXIT_FAILURE);
    }
}

void upscale_freq(int cnum) {
	// Write to freq files.
    for(auto it: container_cores[cnum]) {
	write_core_msr(freq_fd[it], FREQ_ARRAY[MAXSTEP]);
    }
    container_freq[cnum] = MAXSTEP;
}

void downscale_freq(int cnum) {
	// Write to freq files.
    for(auto it: container_cores[cnum]) {
	write_core_msr(freq_fd[it], FREQ_ARRAY[FREQ_STEP_DES]);
    }
    container_freq[cnum] = FREQ_STEP_DES;
}

//=========================================================================================================================================================//
/*
 * Actual controller logic. Implements when and what to upscale/downscale.
 */
//=========================================================================================================================================================//
void downscale(int flow) {
    // For downscale, start with the container that has the smallest expected exec time
    // First downscale frequency for all containers in the flow, then move on to reducing cores.
    float lowest = 1000;
    int idx;
    // Keep variable around to do this check.
    if(!allDown[flow]) {
        // There is at least one container whose freq can be reduced.
        for(auto it: container_flow_array[flow]) {
            if(container_freq[it] != FREQ_STEP_DES) {
                lowest = exec_desired[flow][it];
                idx = it;
            }
        }
        if(lowest != 1000) {
            // Something was found, change the frequency.
            downscale_freq(idx);
            return;
        } else 
            allDown[flow] = true;
    }
}

bool upscale(int flow, int edge) {
   // For upscale, prpeferentially upscale the downstream stuff from the bad edge, then upscale upstream stuff.
   // First see if downstream has any opportunity.
   bool found = false;
   for(int i=0; i<3 && i<downstream_conts[flow][edge].size(); i++) {
	int ctr = downstream_conts[flow][edge][i];
        if(container_freq[ctr] != MAXSTEP) {
	    found = true;
	    upscale_freq(ctr);
	}
   }		    
   if(!found) {
	// Time to upscale starting from upstream, just upscale one of the containers.
        for(auto it: container_flow_array[flow]) {
            if(container_freq[it] != MAXSTEP) {
		found = true;
		upscale_freq(it);
		break;
	    }
        }
   }
   return found;
}

void upscale2(int flow, uint32_t max_edge, uint32_t min_edge) {
   // For upscale, prpeferentially upscale the downstream stuff from the bad edge, then upscale upstream stuff.
   // First see if downstream has any opportunity.
   int found_cnt = 0;
   for(int i=0; i<3 && i<downstream_conts[flow][max_edge].size(); i++) {
	int ctr = downstream_conts[flow][max_edge][i];
        if(container_freq[ctr] != MAXSTEP) {
	    found_cnt++;
	    upscale_freq(ctr);
	}
   }		    
   if(found_cnt < 2)
	return;

   for(int i=0; i<3 && i<downstream_conts[flow][min_edge].size(); i++) {
	int ctr = downstream_conts[flow][min_edge][i];
        if(container_freq[ctr] != MAXSTEP) {
	    found_cnt++;
	    upscale_freq(ctr);
	}
   }		    
   if(found_cnt < 2)
	return;

   // Time to upscale starting from upstream, just upscale one of the containers.
   for(auto it: container_flow_array[flow]) {
       if(container_freq[it] != MAXSTEP) {
	   upscale_freq(it);
	   break;
       }
   }
}

/*
 * Simplest decide() function, only increases frequency of downstream stuff.
 */
void decide() {
    fast_path_detect();

    struct timespec curr_time;
    // In each case, identify the edge that should be chosen for the downscaling. This should be calculated in the detect functions.
    
    // Don't do anything for 5us after making a decision
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &curr_time);
    uint64_t t_flat = curr_time.tv_sec*1000000000 + curr_time.tv_nsec;
 
    for(int flow=0; flow<FLOWS; flow++) {
	if(t_flat - last_upscale_time[flow] < 5000)
	    continue;
	for(int edge=0; edge<MAX_EDGES; edge++) {
	    if(bpf_map[flow*MAX_EDGES+edge] == 1) {
		for(int i=0; i<3 && i<downstream_conts[flow][edge].size(); i++) {
		    upscale_freq(downstream_conts[flow][edge][i]);
		}		    
		*(shmem+flow*MAX_EDGES+edge) = 0;
		last_upscale_time[flow] = t_flat;
		allDown[flow] = false;
	    }
	}
    }
    for(int flow=0; flow<FLOWS; flow++) {
	// At this point it is safe to check whether this flow can be downscaled. Downscale after 15us
	if(!allDown[flow] && t_flat - last_upscale_time[flow] < 15000 && t_flat - last_downscale_time[flow] < 5000) { 
	    downscale(flow);
	    last_downscale_time[flow] = t_flat;
	}
    }
}

/*
 * Bit more complex decide() function, increases frequency of both up and downstream.
 */
void decide2() {
    fast_path_detect();

    struct timespec curr_time;
    // In each case, identify the edge that should be chosen for the downscaling. This should be calculated in the detect functions.
    
    // Don't do anything for 5us after making a decision
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &curr_time);
    uint64_t t_flat = curr_time.tv_sec*1000000000 + curr_time.tv_nsec;
   
    bool found; 
    for(int flow=0; flow<FLOWS; flow++) {
	if(t_flat - last_upscale_time[flow] < 5000)
	    continue;
	for(int edge=0; edge<MAX_EDGES; edge++) {
	    if(bpf_map[flow*MAX_EDGES+edge] == 1) {
		found = upscale(flow, edge);
		*(shmem+flow*MAX_EDGES+edge) = 0;
		last_upscale_time[flow] = t_flat;
		allDown[flow] = false;
		if(!found)	// No point checking more edges if everything has already been upscaled.
		    break;
	    }
	}
    }
    for(int flow=0; flow<FLOWS; flow++) {
	// At this point it is safe to check whether this flow can be downscaled. Downscale after 15us
	if(!allDown[flow] && t_flat - last_upscale_time[flow] < 15000 && t_flat - last_downscale_time[flow] < 5000) { 
	    downscale(flow);
	    last_downscale_time[flow] = t_flat;
	}
    }
}

/*
 * More complex, this uses the version where the bpf code returns the min and max violating edges, i.e. 2 ints per flow.
 */
void decide3() {
    fast_path_detect2();

    struct timespec curr_time;
    // In each case, identify the edge that should be chosen for the downscaling. This should be calculated in the detect functions.
    
    // Don't do anything for 5us after making a decision
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &curr_time);
    uint64_t t_flat = curr_time.tv_sec*1000000000 + curr_time.tv_nsec;
 
    for(int flow=0; flow<FLOWS; flow++) {
	if(t_flat - last_upscale_time[flow] < 5000)
	    continue;
	if(bpf_map2[flow*2] == 0xffffffff && bpf_map2[flow*2+1] == 0xffffffff) { 	// No violation
	    if(t_flat - last_upscale_time[flow] < 15000 && t_flat - last_downscale_time[flow] < 5000) { 
	    	downscale(flow);
	        last_downscale_time[flow] = t_flat;
	    }
	    continue;
	}
	
	// Now upscale in preference order (1) everything below the max (2) everything below the min (3) upstream.
	upscale2(flow, bpf_map2[flow*2], bpf_map2[flow*2+1]);
	*(shmem+flow*2) = 0xffffffff;
	*(shmem+flow*2+1) = 0xffffffff;
	allDown[flow] = false;
	last_upscale_time[flow] = t_flat;
    }
}

//=========================================================================================================================================================//
/*
 * Initialization of system state
 */
//=========================================================================================================================================================//
void init_clusters() {
	// Clustering for ping_chain
	container_cores[0] = {1,2};
	container_core_num[0] = 2;
  
	container_cores[1] = {3,4};
	container_core_num[1] = 2;
  
	container_cores[2] = {5,6};
	container_core_num[2] = 2;

	container_cores[3] = {7,8};
	container_core_num[3] = 2;
  
	container_cores[4] = {9,10};
	container_core_num[4] = 2;

	container_cores[5] = {11,12};
	container_core_num[5] = 2;
	
	container_cores[5] = {13,14};
	container_core_num[5] = 2;

	// Clustering for readUserTimeline.
	//container_cores[0] = {2,3,4};
	//container_core_num[0] = 3;
  
	//container_cores[1] = {5};
	//container_core_num[1] = 1;
  
	//container_cores[2] = {6};
	//container_core_num[2] = 1;

	//container_cores[3] = {7};
	//container_core_num[3] = 1;
  
	//container_cores[4] = {8};
	//container_core_num[4] = 1;

	//container_cores[5] = {9};
	//container_core_num[5] = 1;
  
	//freeCores = 16;
	//freeCores_list = {0,1,10,11,12,13,14,15,16,17,18,19,20,21,22,23};

	// Clustering for composePost
	// Clustering for readPlot
	// Clustering for composeReview
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
            std::vector<uint32_t> down;
            std::getline(file, line);
	    std::cout << line << std::endl;
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

void freq_file_setup() {
    std::string file1 = "/dev/cpu/";
    std::string file2 = "/msr";

    for(int i=0; i<CORES; i++) {
        const char* str = (file1 + std::to_string(i) + file2).c_str();
        freq_fd[i] = open(str, O_WRONLY);
        if(freq_fd[i] < 0) {
            perror("open");
            exit(EXIT_FAILURE);
        }
    }
}

void init_setup() {
    read_config_file();
    std::cout << "config file read\n";
    read_flow_file();
    std::cout << "flow file read\n";
    
    // Set up the arrays to communicate with the kernel shared memory.
    #ifdef USE_KERNEL
    init_mmap((void*)shmem);
    std::cout << "mmap configured for kernel access\n";
    #endif

    // Set up the frequency files.
    freq_file_setup();
    std::cout << "/dev/msr setup done\n";

    init_clusters();

    struct timespec ts1;
    uint64_t t_flat;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts1);
    t_flat = ts1.tv_sec*1000000000 + ts1.tv_nsec;
    for(int i=0; i<FLOWS; i++) {
	last_upscale_time[i] = t_flat;
	last_downscale_time[i] = t_flat;
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
    sleep(10);
    return 0;

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

    #ifdef USE_KERNEL
    clean_mmap((void*)shmem);
    #endif
}
