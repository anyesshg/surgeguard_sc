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

#include "../../gen-cpp/Ping5Service.h"
#include "../../gen-cpp/Ping0Service.h"
#include "../ClientPool.h"
#include "../ThriftClient.h"
#include "../logger.h"
#include "../tracing.h"
#include <time.h>

namespace social_network {

class Ping5Handler : public Ping5ServiceIf {
 public:
  Ping5Handler(ClientPool<ThriftClient<Ping0ServiceClient>> *);
  ~Ping5Handler() override = default;

  void Ping(int8_t, int64_t, int16_t, int64_t) override;

 private:
  std::map<int, float> in_time[1];
  float exec_time, exec2;
  std::atomic<int> cnt;

 private:
  ClientPool<ThriftClient<Ping0ServiceClient>> * _pong_client_pool;
};

Ping5Handler::Ping5Handler(
    ClientPool<ThriftClient<Ping0ServiceClient>> *pong_client_pool) {
  _pong_client_pool = pong_client_pool;

  exec_time = 0; exec2 = 0;
  cnt.store(0);
}

void 
Ping5Handler::Ping(int8_t flow, int64_t timestamp, int16_t level, int64_t req_id) {
   return;
}
}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_SRC_PINGSERVICE_PINGHANDLER_H_
