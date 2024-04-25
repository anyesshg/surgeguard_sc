#define NODE 0
#define CORES 28
#define HALF 32
#define CONTS 32
#define FLOWS 1
#define FREQ_STEP_DES 8
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
#include <cassert>

/*
 * Variables for controller
 */
// Core mapping
int core_state[HALF*2];            
int cpus[CONTS], lim[CONTS];

// For scoring and upscaling.
int score[CONTS], upscaled[CONTS];
int cidx = 0, total_score=0;
//float tsc[CONTS];
//float this_tsc;

// Extras.
int new_sample_cnt = 0;
bool train = true;

// Check for repeated samples.
bool have_new_sample;
float prev_values[CONTS][3];
bool issame[CONTS];

// Desired per-container metrics
float exec_desired[FLOWS][CONTS];		         // Array of desired input and exec time for each container, for each flow.
float upscale_desired[FLOWS][CONTS];		         // Array of desired input and exec time for each container, for each flow.

// Convert to per-cluster, though it is not really used.
float exec_desired_cluster[FLOWS][CONTS];
float new_exec_cluster[FLOWS][CONTS];

// For sensitivity
float exec_avg[FLOWS][CONTS][HALF];
bool alloced[CONTS];
//struct timespec last_upscale_time[FLOWS];

// File names for the various things needed
std::string _cname[CONTS];      // Container name
std::string _snames[CONTS];     // Stats file names
std::string _fnames[CONTS];     // Frequency file names
std::string _limFile = "slack_setup_read";
std::string container_name_actual[CONTS];

// Resource change
std::string stats1 = "/home/cc/paper_setup/shared/";
std::string cpufreq1 = "/sys/devices/system/cpu/cpu";
std::string cpufreq2 = "/cpufreq/scaling_setspeed";
uint32_t FREQ_ARRAY[24] = {800000, 900000, 1000000, 1100000, 1200000, 1300000, 1400000, 1500000, 1600000, 1700000, 1800000, 1900000, 2000000, 2100000, 2200000, 2300000, 2400000, 2500000, 2600000, 2700000, 2800000, 2900000, 3000000, 3100000};

// Things for defining whether resource changes have occured, these are used by the actual resource update fns
bool changed_core[CONTS];

// Initialized things.
int cont_to_cluster[CONTS];       
int num_clusters, num_containers;
int cluster_freqStep[CONTS];    


void read_profile() {
  std::ifstream fpo(_limFile);
  std::string tmpStr;
  int idx;
  float exec, upscale;
  std::string name;

  while(std::getline(fpo, tmpStr)) {
    std::stringstream linestream(tmpStr);
    linestream >> idx >> name >> exec >> upscale;
    if(idx < num_containers) {
	exec_desired[0][idx] = 2.0*exec;
	upscale_desired[0][idx] = 1.2*upscale;
    }
  }
  fpo.close();
}

/*
 * Functions for starting off with a predefined parititoning.
 */
int next = 0;
void init_cores(int idx, int cores) {
  int start = next;
  for(int i=0; i<cores; i++) {
    core_state[start] = idx; core_state[HALF+start] = idx;
    start++;
  }
  next = start;
  cpus[idx] = cores; lim[idx] = cores; changed_core[idx] = true;
}

void init_cluster() {
  next = 0;	// Reset pointer
  for(int i=0; i<CORES; i++) {
    core_state[i] = -1; core_state[i+HALF] = -1;
  }
  for(int i=CORES; i<HALF; i++) { 
    core_state[i] = -2; core_state[i+HALF] = -2;
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
	//std::cout << "Inside: " << owner << " ---> " << _cname[i] << "\n";
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
        if(changed_core[i] && cpus[i] > 0) {
            std::string str = ("docker update --cpuset-cpus " + get_cpu_list(i) + " ");
            for(int it=0; it<num_containers; it++) {
		if(cont_to_cluster[it] == i) {
		  if(get_cpu_list(i) == "") {
			printf("No cores allocated to cluster %d container %d\n", i, it);
			exit(-1);
		  }
		  //std::cout << str << container_name_actual[it] << "\n";
                  system((str + container_name_actual[it]).c_str());
		}
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
          std::cout << "Could not open freq file with name " << c << _fnames[c] << "\n";
          exit(-1);
      }
      fprintf(fp, "%i", FREQ_ARRAY[step]);
      fflush(fp);
      fclose(fp);
    }
  }
}

void add_core(int owner, bool gate_on_sens) {
  if(alloced[owner]){
        printf("Did not add because removed by insensitive %d\n", owner);
        return;
  }

  // If not in train, only add cores if sens is not very low.
  if(!train && gate_on_sens) {
    float sens = 1-exec_avg[0][owner][cpus[owner]+1]/prev_values[owner][0];
    if(exec_avg[0][owner][cpus[owner]+1] > 0) {
      // Always explore.
      if(sens < 0.02)
	return;
    }  
  }

  for(int i=cidx; i<CORES; i++) {
    if(core_state[i] == -1) {
      // Found idle core, reallocate.
      changed_core[owner] = 1;
      core_state[i] = owner; core_state[i+HALF] = owner;
      cidx++;
      cpus[owner]++;
      return;
    }
  }
  for(int i=0; i<cidx; i++) {
    if(core_state[i] == -1) {
      // Found idle core, reallocate.
      changed_core[owner] = 1;
      core_state[i] = owner; core_state[i+HALF] = owner;
      cidx++;
      cpus[owner]++;
      return;
    }
  }
}

void yield_multi_cores(int owner, int final_cores) {
  changed_core[owner] = 1;
  
  for(int i=cidx; i<CORES; i++) {
    if(core_state[i] == owner) {
      core_state[i] = -1; core_state[i+HALF] = -1;
      cpus[owner]--;
      if(cpus[owner] <= final_cores) {
	cidx = i;
	return;
      }
    }
  }
  for(int i=0; i<cidx; i++) {
    if(core_state[i] == owner) {
      core_state[i] = -1; core_state[i+HALF] = -1;
      cpus[owner]--;
      if(cpus[owner] <= final_cores) {
	cidx = i;
	return;
      }
    }
  }
}

void yield_core(int owner) {
  changed_core[owner] = 1;
  for(int i=cidx; i<CORES; i++) {
    if(core_state[i] == owner) {
      // Found idle core, reallocate.
      core_state[i] = -1; core_state[i+HALF] = -1;
      cpus[owner]--;
      cidx++;
      return;
    }
  }
  for(int i=0; i<cidx; i++) {
    if(core_state[i] == owner) {
      // Found idle core, reallocate.
      core_state[i] = -1; core_state[i+HALF] = -1;
      cidx++;
      cpus[owner]--;
      return;
    }
  }
}
void remove_insensitive() {
  for(int i=0; i<FLOWS; i++) {
    // Remove cores from the clusters with low sensitivity.
    for(int j=0; j<num_clusters; j++) {
      bool cpus_ok = (cpus[j] > lim[j]);
      if(cpus_ok) {
  	    bool lower_found = false;
  	    int m=1;
  	    int best_found = 0;
  	    for(int k=cpus[j]-1; k>=lim[j]; k--) {
  	      if(exec_avg[i][j][k] > 0 && exec_avg[i][j][k] < exec_avg[i][j][cpus[j]]/(1-(0.02*m))) {	// Each step should contribute at least 2% benefit, if it doesn't, remove a core
  	        lower_found = true;
  	        best_found = k;
  	      }
  	      m++;
  	    }

  	    if(lower_found) {
  	      // Remove multiple cores for the low exec clusters, and 1 core for tje high exec cluster.s
  	      printf("Removing core from cluster %d\n", j);

  	      if(exec_desired_cluster[i][j] < 1)	// Can be called only for post-mongodb.
  		      yield_multi_cores(j,best_found);
  	      else {
		if(cpus[j]-best_found > 3)
		  yield_multi_cores(j, cpus[j]-2);
		else	
  	   	  yield_core(j);
	      }
  	      alloced[j] = true;
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

void read_stats_file() {
  bool exec_issue_found = false;
  for(int i=0; i<num_clusters; i++)
    issame[i] = 1;

  for(int i=0; i<num_containers; i++) {
    issame[i] = false; alloced[i] = false; score[i] = 1;
    new_exec_cluster[0][i] = 0;
  }
  have_new_sample = true;
  total_score = 0;

  int same = 0; int read = 0;
  // Read all the stats files.
  for(int i=0; i<num_containers; i++) {
    bool isSame = false;
    int cluster = cont_to_cluster[i];
    std::ifstream fp(_snames[i]);
    if(fp.fail()) {
	    ;
    } 
    else {
      std::string tmpStr;
      std::getline(fp, tmpStr);
      if(tmpStr.size() < 5) {
	      // Not reading real values yet, just set exec_okay
	      continue;
      }
      read++;
      std::stringstream linestream(tmpStr);
      float upscale, exec, execm;

      linestream >> execm >> exec >> upscale;
      if(exec == prev_values[i][0] && execm == prev_values[i][1]) {
        same++;
        issame[cluster] = true;
	isSame = true;
        continue;
      }
      prev_values[i][0] = exec;
      prev_values[i][1] = execm;
      new_exec_cluster[0][cluster] += execm;
  
      float queue_metric = upscale/upscale_desired[0][i];
      float _exec_metric = execm/exec_desired[0][i];

      if((exec_desired_cluster[0][cluster] < 0.01)) {
	fp.close();
	continue;
      }

      float Q_HIGH = 1.03; float Q_LOW = 0.98;
      
      // Score of each thing starts off at 1.
      if(upscale_desired > 0) {
        if(queue_metric > Q_HIGH) {
          score[cluster]++;
        }

        if(queue_metric < Q_LOW) {
	  score[cluster] = 0;
        }
      }
      float HIGH = 1.05;
      float LOW = 0.95;

      // For really short services/the mongodbs, make it more difficult to raise the resources.
      if(_exec_metric > HIGH) {
	score[cluster]++;
      }
      if(_exec_metric < LOW) {
	score[cluster] = 0;
      }
    }
    fp.close();
  }

  if(same == read)	
     have_new_sample = false;
  else {
    for(int i=0; i<num_clusters; i++) 
	total_score += score[i];

    // Set the exec_avg.
    for(int i=0; i<num_clusters; i++) {
      float sum = 0;
      for(int j=0; j<num_containers; j++) {
        if(cont_to_cluster[j] == i && prev_values[j][0] > 0)
  	  sum += prev_values[j][0];
      }	
      if(exec_avg[0][i][cpus[i]] != -1)
        exec_avg[0][i][cpus[i]] = 0.55*exec_avg[0][i][cpus[i]] + 0.45*sum;
      else
	exec_avg[0][i][cpus[i]] = sum;
    }
  }
}
//=========================================================================================================================================================//
/*
 * Controller version 2
 */
//=========================================================================================================================================================//
void upscale2() {
  while(1){
    // Upscale starting from the highest score.
    int maxScore = -1;
    float maxExec = -1;
    int idx = -1;

    for(int i=0; i<num_clusters; i++) {
      if(score[i] > maxScore && !upscaled[i]) {
        maxExec = new_exec_cluster[0][i];
        maxScore = score[i];
        idx = i;
      }
      if(score[i] == maxScore && !upscaled[i] && maxExec < new_exec_cluster[0][i]) {
        maxExec = new_exec_cluster[0][i];
        idx = i;
      }
    }
    if(idx == -1 || maxScore <= 1)	// score should be at least 2 for upscaling.
      break;
    upscaled[idx] = true;
    if(cluster_freqStep[idx] != MAXSTEP) {
      update_freq(idx, MAXSTEP);
    } else {
      add_core(idx, false);
    }
  }
}

void downscale2() {
  // For downscale, start with the container that has the smallest expected exec time
  // First downscale frequency for all containers in the flow, then move on to reducing cores.
  // Keep variable around to do this check.
  bool ds = false;
  for(int i=0; i<num_containers; i++) {
    if(cluster_freqStep[i] > FREQ_STEP_DES + 4) {
      update_freq(i, cluster_freqStep[i]-4);
      ds = true;
    }
  }
  if(ds)  return;
 
  // Start with the cluster with largest diff between actual and desired. 
  float best_diff = 2.0; int found = -1;
  for(int cluster=0; cluster < num_clusters; cluster++) {
    float diff = exec_avg[0][cluster][cpus[cluster]]/exec_desired_cluster[0][cluster];
    if(cpus[cluster]>lim[cluster] && diff < 1 && !issame[cluster]) {
    	if(diff < best_diff) {
    	  best_diff = diff;
    	  found = cluster;
    	}
    }
  }
  if(found >= 0) {
    yield_core(found);
  }
}

void decide2() {
  read_stats_file();
  if(!have_new_sample)
    return;

  new_sample_cnt++;

  if(new_sample_cnt%3 == 2) {
    // Remove insensitive even if there is flow slowdown.
    remove_insensitive();
  }

  struct timespec st;

  if(total_score == 0)
     downscale2();
  else
     upscale2();
  
  do_all_allocations();
}

//=========================================================================================================================================================//
/*
 * Configuration and initialization functions
 */
//=========================================================================================================================================================//

void set_up_names() {
    int t_num_clusters, t_num_containers;

    std::ifstream file("/home/cc/paper_setup/config/config_cluster");	// Change to name of the config file.

    // First read the file and find the actual container names. Write them into file "container_names". Not needed if container names match what is provided in the config file.
    std::string line, line2;
    std::getline(file, line);

    t_num_containers = std::stoi(line);
    std::getline(file, line);
    t_num_clusters = std::stoi(line);
    for(int i=0; i<t_num_containers; i++) {
      std::getline(file, line);
      std::stringstream linestream(line);

      std::string data, name;
      int id, this_node;

      linestream >> id >> this_node >> name >> data;
      if(this_node != NODE)
        continue;

      std::system(("docker inspect --format '{{.Name}}' $(docker ps -q) | grep " +  name + ">> container_names").c_str());
    }
    file.close();
}

void initialize() {
    for(int i=0; i<CONTS; i++) {
        upscaled[i] = false;
	cluster_freqStep[i] = 7;
    }

  init_cluster();

  int t_num_clusters, t_num_containers;
  std::ifstream file("/home/cc/paper_setup/config/config_social_cluster");
  std::ifstream file2("container_names");
  std::string line, line2;
  std::getline(file, line);
  t_num_containers = std::stoi(line);
  std::getline(file, line);
  t_num_clusters = std::stoi(line);
    
  num_clusters = 0; num_containers = 0;

  // Read the input file and set up things.
  int cnt=0; float discard;
  for(int i=0; i<t_num_containers; i++) {

    std::getline(file, line);
    std::stringstream linestream(line);
        
      int id, this_node;
      std::string name;
      int clusterID; int num_cores;

      linestream >> id >> this_node >> name;
      if(this_node != NODE)
        continue;
      
      std::getline(file2, line2); 
      _cname[cnt] = name;
      container_name_actual[cnt] = line2;
    
    for(int j=0; j<FLOWS; j++) {
      linestream >> exec_desired[0][cnt] >> discard;
    }

    linestream >> clusterID >> num_cores;
    cont_to_cluster[cnt] = clusterID;
    exec_desired_cluster[0][clusterID] += exec_desired[0][cnt];
    _snames[cnt] = stats1+name;
 
    init_cores(clusterID, num_cores);
    if(clusterID > num_clusters)
      num_clusters = clusterID;

    num_containers++;
    cnt++;
  }
    num_clusters++;
    file.close();
    file2.close();
  
}

void file_setup() {
  std::string file1 = "/home/cc/paper_setup/shared/";
  for(int i=0; i<num_containers; i++) {
    _snames[i] = file1 + _cname[i];
  }
}

void init_setup() {
  initialize();
 
  // Set up the communication pipes.
  file_setup();

  // Set up the frequency files.
  for(int i=0; i<CONTS; i++) {
    _fnames[i] = cpufreq1+std::to_string(i)+cpufreq2;
  }

  // Initialize the other arrays.
  for(int i=0; i<FLOWS; i++) {
    //clock_gettime(CLOCK_REALTIME, &last_upscale_time[i]);
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
    int cnt_sanity = 0;
    struct timespec ts1, ts2, ts3;
    int i=0;

    float warmup = 30000;
    bool warmed = false;

    set_up_names();
    init_setup();
    do_all_allocations();
    read_profile();

    clock_gettime(CLOCK_REALTIME, &ts1);
   
    // Warm for 30s and then poll for 60s.
    do {
       clock_gettime(CLOCK_REALTIME, &ts2);
	i++;
       for(int ii=0; ii<num_clusters; ii++) {
           changed_core[ii] = false;
           upscaled[ii] = false;
       }
       decide2();
       usleep(100000);

       core_sum = 0;
       for(int ii=0; ii<HALF*2; ii++) {
	   if(core_state[ii] >= 0) {
	    core_sum++;
	   }
       }
	clock_gettime(CLOCK_REALTIME, &ts3);
	if ((ts3.tv_sec-ts1.tv_sec)*1000 + (ts3.tv_nsec-ts1.tv_nsec)/1000000.0 < warmup) {
	  full_sum += core_sum*((ts3.tv_sec-ts2.tv_sec)*1000000 + (ts3.tv_nsec-ts2.tv_nsec)/1000.0);
	  full_time += 1*((ts3.tv_sec-ts2.tv_sec)*1000000 + (ts3.tv_nsec-ts2.tv_nsec)/1000.0);
	} else {
	  if(!warmed) {
	    warmed = true;
	    printf("Resetting cluster\n");
	    init_cluster();
	    do_all_allocations();
 	    train = false;
    	    printf("Warmup Phase: CoreUsage (us-cores): %f, avg cores used: %f\n", full_sum, full_sum/full_time);
	    full_sum = 0; full_time = 0;
	  }
	  full_sum += core_sum*((ts3.tv_sec-ts2.tv_sec)*1000000 + (ts3.tv_nsec-ts2.tv_nsec)/1000.0);
	  full_time += 1*((ts3.tv_sec-ts2.tv_sec)*1000000 + (ts3.tv_nsec-ts2.tv_nsec)/1000.0);
	}
   } while (((ts3.tv_sec-ts1.tv_sec)*1000+(ts3.tv_nsec-ts1.tv_nsec)/1000000.0) < 90000);  
    printf("CoreUsage (us-cores): %f, avg cores used: %f\n", full_sum, full_sum/full_time);
    printf("============Sensitivity=============\n");

  for(int i=0; i<FLOWS; i++) {
    for(int j=0; j<num_containers; j++) {
      for(int k=1; k<20; k++) {
        float sens = exec_avg[i][j][k];
        printf("%f ", sens);
      }
      printf("\n");
    }
  }
    printf("============Sensitivity=============\n");
    return -1;
}
