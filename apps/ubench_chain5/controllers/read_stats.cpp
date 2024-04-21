#define CORES 24
#define CONTS 32

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

// Setup and node state
int num_containers;
int num_flows;

// File names for the various things needed
std::string _cname[CONTS];      // Container name
std::string _snames[CONTS];     // Stats file names

int num_clusters;                   
float in_time[CONTS], exec_time[CONTS], exec2_time[CONTS]; 
float max_in_time[CONTS], max_exec_time[CONTS], max_exec2_time[CONTS];
int place[CONTS];

std::string stats1 = "/home/cc/paper_setup/shared/";

void read_stats_file() {
  float blah1, blah2, blah3;
  // Expected input: avg_exec long_input short_input requests
  // Expected input for multi-flow: avg_exec <long_input short_input requests> <...> <...> ...
  // Read all the stats files.
  //assert(num_containers == 6);
  for(int i=0; i<num_containers-1; i++) {
    //assert(_snames[i] != (stats1+"ping-service-5"));
    std::ifstream fp(_snames[i]);
    if(fp.is_open()) {
      std::string tmpStr;
      std::getline(fp, tmpStr);
      if(tmpStr.size() == 0)
	continue;

      std::stringstream linestream(tmpStr);
      linestream >> blah1 >> blah2 >> blah3;
      if(i == 0 && blah3 != 0) {
	std::cout << tmpStr.size() << tmpStr << std::endl;
	assert(_snames[i] == (stats1+"ping-service-0"));
	std::system("cat ~/paper_setup/shared/ping-service-0");
        std::cout << blah1 << "," << blah2 << "," << blah3 << std::endl;
      }
      exec_time[i] += blah1;
      exec2_time[i] += blah2;
      in_time[i] += blah3;
      if(blah1 > max_exec_time[i])
	max_exec_time[i] = blah1;
      if(blah2 > max_exec2_time[i])
	max_exec2_time[i] = blah2;
      if(blah3 > max_in_time[i])
	max_in_time[i] = blah3;
      fp.close();
    }
  }
}


void initialize() {
    // Read input file and populate inputs. Each line has format {id name target_latency cluster_id}
    std::ifstream file("/home/cc/paper_setup/config/config_ping_cluster2");
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
	int clusterID;
        float blah1, blah2;
	std::string name;

        linestream >> id >> name;
	if(name == "nginx-thrift")
	    continue;

	_cname[i] = name;
	for(int j=0; j<1; j++) {
	    linestream >> blah1 >> blah2;
	}
	linestream >> clusterID >> place[i];
        _snames[i] = stats1+name;
        std::cout << _snames[i] << std::endl;
    }
}

/*
 * Main() function
 */
int main(int argc, char* argv[]) {
    initialize();
    
    for(int i=0; i<num_containers; i++) {
	exec_time[i] = 0; exec2_time[i] = 0; in_time[i] = 0;
	max_exec_time[i] = -1; max_exec2_time[i] = -1; max_in_time[i] = -1;
    }

    sleep(10);
    int cnt=0;
    struct timespec ts1, ts2, ts3;
    clock_gettime(CLOCK_REALTIME, &ts1);
   
    do {
       clock_gettime(CLOCK_REALTIME, &ts2);
       cnt++;
       read_stats_file();

       usleep(500);
       clock_gettime(CLOCK_REALTIME, &ts3);

   } while (((ts3.tv_sec-ts1.tv_sec)*1000+(ts3.tv_nsec-ts1.tv_nsec)/1000000.0) < 25000);  

    printf("In-time\n");
    for(int j=0; j<num_containers; j++) {
	printf("%f ", in_time[j]/cnt);
    }
    printf("\n");
    printf("Exec-time\n");
    for(int j=0; j<num_containers; j++) {
	printf("%f ", exec_time[j]/cnt);
    }
    printf("\n");
    printf("Exec2-time\n");
    for(int j=0; j<num_containers; j++) {
	printf("%f ", exec2_time[j]/cnt);
    }
    printf("\n\n");
    printf("In-time-scaled(1.5)\n");
    for(int j=0; j<num_containers; j++) {
	printf("%f ", 1.5*in_time[j]/cnt);
    }
    printf("\n");
    printf("exec-time-scaled(2)\n");
    for(int j=0; j<num_containers; j++) {
	printf("%f ", 2*exec_time[j]/cnt);
    }
    printf("\n");
    printf("Exec2-time\n");
    for(int j=0; j<num_containers; j++) {
	printf("%f ", 2*exec2_time[j]/cnt);
    }
    printf("\n\n");
    printf("\n\n");
    printf("max_in_time\n");
    for(int j=0; j<num_containers; j++) {
	printf("%f ", max_in_time[j]);
    }
    printf("\n");
    printf("max_exec_time\n");
    for(int j=0; j<num_containers; j++) {
	printf("%f ", max_exec_time[j]);
    }
    printf("\n");
    printf("max_exec2_time\n");
    for(int j=0; j<num_containers; j++) {
	printf("%f ", max_exec2_time[j]);
    }
    printf("\n\n");
    return -1;
}
