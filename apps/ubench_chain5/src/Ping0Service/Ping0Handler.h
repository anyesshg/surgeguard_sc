#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_PINGSERVICE_PINGHANDLER_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_PINGSERVICE_PINGHANDLER_H_
#define ARRS 4096
//#define SPIKE 1

#include <chrono>
#include <iostream>
#include <string>
#include <fstream>
#include <map>
#include <atomic>
#include <mutex>
#include <cstdlib>

#include "../../gen-cpp/Ping0Service.h"
#include "../../gen-cpp/Ping1Service.h"
#include "../ClientPool.h"
#include "../ThriftClient.h"
#include "../logger.h"
#include "../tracing.h"
#include <time.h>

namespace social_network {

class Ping0Handler : public Ping0ServiceIf {
 public:
  Ping0Handler(ClientPool<ThriftClient<Ping1ServiceClient>> *);
  ~Ping0Handler() override = default;

  void Ping(int8_t, int64_t, int16_t, int64_t) override;

 private:
  std::map<int, float> in_time[1];
  float exec_time, exec2;
  std::atomic<int> cnt;
  std::mutex mymutex;
 
 private:
  ClientPool<ThriftClient<Ping1ServiceClient>> * _pong_client_pool;
};

Ping0Handler::Ping0Handler(
    ClientPool<ThriftClient<Ping1ServiceClient>> *pong_client_pool) {
  _pong_client_pool = pong_client_pool;

  exec_time = 0; exec2 = 0;
  cnt.store(400);
}

void 
Ping0Handler::Ping(int8_t flow, int64_t timestamp, int16_t level, int64_t req_id) {
  struct timespec ts, ts2, tse;
  clock_gettime(CLOCK_REALTIME, &ts);
  
  int64_t new_ts = timestamp; 
  new_ts = (int64_t)(ts.tv_sec*1000000000 + ts.tv_nsec);	// Scale = 1ns
  
  cnt++;
  float this_in_time = (float)(ts.tv_sec*1000000000 + ts.tv_nsec - new_ts)/(float)1000.0;
  auto it = in_time[0].find(level);
  if(it != in_time[0].end()) 
	it->second = it->second*0.995 + 0.005*this_in_time;
  else
	in_time[0][level] = this_in_time;

  int sum = 0;
  int _reps = (level < 5) ? 5 : level;
  for(int i=0; i<_reps; i++) {
    for(int j=0; j<ARRS*_reps; j++)
	sum += j;
  }
  
  clock_gettime(CLOCK_REALTIME, &tse);
  
  auto pong_client_wrapper = _pong_client_pool->Pop();

  if (!pong_client_wrapper) {
    LOG(error) << "Failed to connect to ping-service";
    throw;
  }

  auto pong_client = pong_client_wrapper->GetClient();
  clock_gettime(CLOCK_REALTIME, &ts2);

  try {
    uint64_t new_req_id = (uint64_t)sum;
    pong_client->Ping(flow, new_ts, level+1, new_req_id);
  } catch (...) {
    LOG(error) << "Failed to send to pong-service";
    _pong_client_pool->Remove(pong_client_wrapper);
    throw;
  }
  _pong_client_pool->Keepalive(pong_client_wrapper);

  exec_time = 0.995*exec_time + 0.005*((tse.tv_sec-ts.tv_sec)*1000000 + (tse.tv_nsec - ts.tv_nsec)/1000.0);
  exec2 = 0.995*exec2 + 0.005*((ts2.tv_sec-ts.tv_sec)*1000000 + (ts2.tv_nsec - ts.tv_nsec)/1000.0);

  if(cnt.load() >= 400) {
      mymutex.lock();
    // Recheck condition.
      if(cnt.load() >= 400) {
	std::string str = std::to_string(exec_time) + " " + std::to_string(exec2) + " " + std::to_string(in_time[0][0]);
        std::system(("echo " + str + " > /bpf-microservices/shared/ping-service-0").c_str());
        cnt.store(0);
     } 
     mymutex.unlock();
  }
  return;
}
}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_SRC_PINGSERVICE_PINGHANDLER_H_
