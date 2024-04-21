#define MY_NODE_ID 0

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
#include "change_freq.h"
#include "kmod_interact.h"

/*
 * Variables for controller
 */
std::map<uint32_t, std::vector<uint32_t>> downstream_conts[FLOWS];	// map indexed by edgeID

int container_freq[CONTS];
int container_core_num[CONTS];
std::vector<int> container_cores[CONTS];

int downstream[FLOWS][3];
bool fp_issue[FLOWS], sl_issue[FLOWS], st_issue[FLOWS];
int fp_loc_min[FLOWS], fp_loc_max[FLOWS], sl_loc[FLOWS], st_loc[FLOWS];
float slow_status[FLOWS*2];		// Reads 2 fp32 numbers per flow.

float container_sens[FLOWS][CONTS][CORES];
bool allUp[FLOWS], allDown[FLOWS];
struct timespec last_upscale_time[FLOWS];
int freeCores;
std::vector<int> freeCores_list;

//TODO TODO
float expected_queue[CORES];
float expected_exec[CORES];

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
uint32_t bpf_map[CORES];

//Variables for measuring and updating frequency.
int freq_fd[CORES];
uint64_t FREQ_ARRAY[24] = {800000000, 900000000, 1000000000, 1100000000, 1200000000, 1300000000, 1400000000, 1500000000, 1600000000, 1700000000, 1800000000, 1900000000, 2000000000, 2100000000, 2200000000, 2300000000, 2400000000, 2500000000, 2600000000, 2700000000, 2800000000, 2900000000, 3000000000, 3100000000};

// Variables for interacting with the containers through pipes
int pipes[CONTS];

// Variables for interacting with the containers through files
const char* dataf[CONTS];


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
 * Given an edge and a flow, return a list of 3 downstream containers regardless of node.
 */
void downstream_on_this_node(int edge, int flow) {
    auto it = downstream_conts[flow].find(edge);
    if(it == downstream_conts[flow].end()){
	downstream[flow][0] = downstream[flow][1] = downstream[flow][2] = -1;
    }
    else {
	int j=0;
	for(int i=0; j<3 && i<it->second.size(); i++) {
	    if(place[(it->second)[i]] == MY_NODE_ID) {
		downstream[flow][j] = (it->second)[i];
		j++;
	    }
        }
	for(int k=j; k<3; k++)
	    downstream[flow][k] = -1;
    }
}

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

/*
 * Functions for detecting things on the slow path 
 */
void slow_path_detect_pipe() {
    // Read all the pipes.
    int nbytes;
    for(int i=0; i<CONTS; i++) {
    	if(pipes[i] != 0) {
	    ioctl(pipes[i], FIONREAD, &nbytes);
	    if(nbytes >= 8*FLOWS) {  // Should read exec_time and input_time.
		read(pipes[i], slow_status, 8);
		for(int j=0; j<FLOWS; j++) {
		    if(slow_status[j*2+1]/expected_queue[i] > 10) {
			st_issue[j] = true;
			st_loc[j] = i;
		    }
		    if(slow_status[j*2]/expected_exec[i] > 1.05) {
			sl_issue[j] = true;
			sl_loc[j] = i;
		    }
	    	}
	    }
     	}
    }
}

void slow_path_detect_file() {
    // Read all the files.
    float exec_time, in_time;
    for(int i=0; i<CONTS; i++) {
	if(place[i]) {
	    FILE* fp = fopen(dataf[i], "r");
	    if(fp != NULL) {
		fscanf(fp,"%f %f", &exec_time, &in_time);
	    } 
	}
    }
}

//=========================================================================================================================================================//
/*
 * Basic upscaling and downscaling functions, does not include the actual controller algo to upscale/downscale
 */
//=========================================================================================================================================================//
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

inline std::string get_cpu_list(int i) {
    std::string str = "";
	if(container_core_num[i] > 0) {
	    str += std::to_string(container_cores[i][0]);
	}

    for(int j=1; j < container_core_num[i]; j++) {
        str += "," + std::to_string(container_cores[i][j]);
    }
    return str;
} 

void upscale_cores(int cnum) {
    int realloc_core;
    if(freeCores < 0) {
        return;
    }
    realloc_core = freeCores_list.back();
    freeCores_list.pop_back();
    freeCores--;

    // Do docker update.
    container_cores[cnum].push_back(realloc_core);
    container_core_num[cnum]++;
    std::string str = "docker update --cpuset-cpus " + get_cpu_list(cnum) + " " + container_name_array[cnum];
    system(str.c_str());

    // Do frequency update.
    write_core_msr(freq_fd[realloc_core], FREQ_ARRAY[MAXSTEP]);
}


//=========================================================================================================================================================//
/*
 * Actual controller logic. Implements when and what to upscale/downscale.
 */
//=========================================================================================================================================================//
void upscale_slow(int container, int flow, bool downstream_only, bool container_only) {
    if(container_only) {
        // Only upscale this container.
        if(container_freq[container] != MAXSTEP) {
            upscale_freq(container);
        } else {
            upscale_cores(container);
        }
    }

    if(downstream_only) {
        // Only upscale downstream containers.
        bool found = false;
        for(int i=0; i<3; i++) {
            if(container_freq[downstream[flow][i]] != MAXSTEP) {
                upscale_freq(downstream[flow][i]);
                found = true;
            }
        }
        if(!found) {
            // Give cores to the downstream container that can benefit the most.
            float sens = -2;
            int highest;
            for(int i=0; i<3; i++) {
                int ctr = downstream[flow][i];
                float se = container_sens[flow][ctr][container_core_num[ctr]];
                if(se > sens) {
                    sens = se;
                    highest = i;
                }
            }
            upscale_cores(downstream[flow][highest]);
        }
    }
    else {
        // Upscale anything based on frequency.
        // Then upscale any containers based on their sensitivity to core changes.
        bool found = false;
        if(!allUp[flow]) {
            for(auto it : container_flow_array[flow]) {
                if(container_freq[it] != MAXSTEP) {
                    upscale_freq(it);
                    found = true;
                }
            }
        }
        if(!found) {
            if(!allUp[flow])
                allUp[flow] = true;
            // Give cores to the container in the flow that can benefit the most.
            float sens = -2;
            int highest;
            for(auto it : container_flow_array[flow]) {
                float se = container_sens[flow][it][container_core_num[it]];
                if(se > sens) {
                    sens = se;
                    highest = it;
                }
            }
            upscale_cores(highest);
        }
    }
}

void downscale(int flow) {
    // For downscale, start with the container that has the smallest expected exec time
    // First downscale frequency for all containers in the flow, then move on to reducing cores.
    float lowest = 1000;
    int idx;
    // Keep variable around to do this check.
    if(!allDown[flow]) {
        // There is at least one container whose freq can be reduced.
        for(auto it: container_flow_array[flow]) {
            if(exec_desired[it] < lowest && container_freq[it] != FREQ_STEP_DES) {
                lowest = exec_desired[it];
                idx = it;
            }
        }
        if(lowest != 1000) {
            // Something was found, change the frequency.
            downscale_freq(idx);
            return;
        } else 
            allDown[flow] = true;
    } else {
        // Remove cores, starting from the container with the least core sensitivity.
        float lowest = 1000;
        int idx;
        for(auto it: container_flow_array[flow]) {
            if(container_sens[flow][it][container_core_num[it]] < lowest && container_core_num[it] > 1) {
                lowest = container_sens[flow][it][container_core_num[it]];
                idx = it;
            }
        }
        // Reduce core from idx.
        int realloc_core = container_cores[idx].back();
        freeCores_list.push_back(realloc_core);
        freeCores++;
        container_cores[idx].pop_back();
        container_core_num[idx]--;
    }
}

/*
 * After getting the fast and slow path info, decide what to upscale.
 */
void decide() {
    fast_path_detect();
    #ifdef USE_PIPES
    slow_path_detect_pipe();
    #else
    slow_path_detect_file();
    #endif

    bool upscaled_any = false;
    // In each case, identify the edge that should be chosen for the downscaling. This should be calculated in the detect functions.
    // Only do things based on fp_issue if sl_issue and st_issue are not pressent
    for(int i=0; i<FLOWS; i++) {
        if(fp_issue[i] && !sl_issue[i] && !st_issue[i]) {
            // Rapidly upscale downstream.
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &last_upscale_time[i]);
            downstream_on_this_node(fp_loc_min[i], i);
            upscale_freq(downstream[i][0]);
            upscale_freq(downstream[i][1]);
            upscale_freq(downstream[i][2]);
            upscaled_any = true;
        }
    }
    
    // Now upscale things which were detected in the slow path
    for(int i=0; i<FLOWS; i++) {
        // Issue has also been detected on the slow path, use the slow path info only to fiure things out.
        if(sl_issue[i] && !st_issue[i]) {
            // Upscale anything.
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &last_upscale_time[i]);
            int edge = node_to_edge(st_loc[i], i);
            downstream_on_this_node(edge, i);
            upscale_slow(-1, -1, false, false);
            upscaled_any = false;
        }
        else if(st_issue[i]) {
            // Upscale downstream only
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &last_upscale_time[i]);
            int edge = node_to_edge(st_loc[i], i);
            downstream_on_this_node(edge, i);
            upscale_slow(-1, -1, true, false);
            upscaled_any = false;
        }
    }

    // Now check for downscaling options.
    if(!upscaled_any) {
        struct timespec st;
        // To be safe, only downscale if no flows have reported any issues.
        for(int i=0; i<FLOWS; i++) {
            if(!sl_issue[i] && !st_issue[i] && !fp_issue[i]) {
                clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &st);
                float passed = (st.tv_sec-last_upscale_time[i].tv_sec)*1000000 + ((float)st.tv_nsec-last_upscale_time[i].tv_nsec)/1000.0; 
                if(passed > 50) {    // Deallocate every 50 usec.
                    downscale(i);
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
void init_clusters() {
	// Clustering for readUserTimeline.
	container_cores[0] = {2,3,4};
	container_core_num[0] = 3;
  
	container_cores[1] = {5};
	container_core_num[1] = 1;
  
	container_cores[2] = {6};
	container_core_num[2] = 1;

	container_cores[3] = {7};
	container_core_num[3] = 1;
  
	container_cores[4] = {8};
	container_core_num[4] = 1;

	container_cores[5] = {9};
	container_core_num[5] = 1;
  
	freeCores = 16;
	freeCores_list = {0,1,10,11,12,13,14,15,16,17,18,19,20,21,22,23};

	// Clustering for composePost
	// Clustering for readPlot
	// Clustering for composeReview
}

void do_all_allocations() {
	// Enforce CPU and core allocations for all containers.
	// Do docker update.
	for(int i=0; i<CONTS; i++) {
		if(place[i] == MY_NODE_ID) {
			std::string st = ("docker update --cpuset-cpus " + get_cpu_list(i) + " " + container_name_array[i]);
			system(st.c_str());
			downscale_freq(i);
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
            std::vector<uint32_t> down;
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

void pipe_setup() {
    for(int i=0; i<CONTS; i++) {
        if(place[i] == MY_NODE_ID) {
            const char* myfifo = ("/home/cc/paper_setup/pipes/"+container_name_array[i]).c_str();
            int ret = mkfifo(myfifo, 0777);
	    if(ret != 0) {
		perror("pipe open");
		exit(EXIT_FAILURE);
	    }
            pipes[i] = open(myfifo, O_RDONLY);
        } else {
            pipes[i] = -1;	// Matches with what happens if error occurs in open()
        }
    }
}

void file_setup() {
    std::string file1 = "/home/cc/paper_setup/shared/";
    for(int i=0; i<num_containers; i++) {
	dataf[i] = (const char*)(file1 + container_name_array[i]).c_str();
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
    read_flow_file();
    
    // Set up the communication pipes.
    #ifdef USE_PIPES
    pipe_setup();
    #else
    file_setup();
    #endif

    // Set up the arrays to communicate with the kernel shared memory.
    #ifdef USE_KERNEL
    init_mmap((void*)shmem);
    #endif

    // Set up the frequency files.
    freq_file_setup();

    // Initialize the other arrays.
    for(int i=0; i<FLOWS; i++) {
        allUp[i] = false;
        allDown[i] = true;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &last_upscale_time[i]);
        for(int ii=0; ii<CONTS; ii++) {
            for(int iii=0; iii<CORES; iii++) {
                container_sens[i][ii][iii] = -1;
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

    clean_mmap((void*)shmem);
}
