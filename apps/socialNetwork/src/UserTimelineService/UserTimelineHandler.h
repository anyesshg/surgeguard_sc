#ifndef SOCIAL_NETWORK_MICROSERVICES_SRC_USERTIMELINESERVICE_USERTIMELINEHANDLER_H_
#define SOCIAL_NETWORK_MICROSERVICES_SRC_USERTIMELINESERVICE_USERTIMELINEHANDLER_H_

#include <bson/bson.h>
#include <mongoc.h>
#include <sw/redis++/redis++.h>

#include <future>
#include <iostream>
#include <string>
#include <chrono>
#include <cassert>
#include <time.h>
#include <mutex>
#include <atomic>
#include <map>

#include "../../gen-cpp/PostStorageService.h"
#include "../../gen-cpp/UserTimelineService.h"
#include "../ClientPool.h"
#include "../ThriftClient.h"
#include "../logger.h"
#include "../tracing.h"

using namespace sw::redis;
using std::chrono::high_resolution_clock;
using std::chrono::duration_cast;
using std::chrono::duration;
using std::chrono::microseconds;

namespace social_network {
class UserTimelineHandler : public UserTimelineServiceIf {
 public:
  UserTimelineHandler(Redis *, mongoc_client_pool_t *,
                      ClientPool<ThriftClient<PostStorageServiceClient>> *);
  UserTimelineHandler(RedisCluster *, mongoc_client_pool_t *,
                      ClientPool<ThriftClient<PostStorageServiceClient>> *);
  ~UserTimelineHandler() override = default;

  void WriteUserTimeline(
    const int8_t flow, const int64_t timestamp2, const int16_t level,
      int64_t req_id, int64_t post_id, int64_t user_id, int64_t timestamp,
      const std::map<std::string, std::string> &carrier) override;

  void ReadUserTimeline(
			std::vector<Post> &,
    			const int8_t, const int64_t, const int16_t,
			int64_t, int64_t, int, int,
                        const std::map<std::string, std::string> &) override;
  void Print() override;
  void Reset() override;
 private:
  Redis *_redis_client_pool;
  RedisCluster *_redis_cluster_client_pool;
  mongoc_client_pool_t *_mongodb_client_pool;
  ClientPool<ThriftClient<PostStorageServiceClient>> *_post_client_pool;
  std::atomic<int> cnt;

  sstats stats[3];
  std::mutex mymutex;

  FILE* ff;
};

void UserTimelineHandler::Print() {
  fflush(ff);
}

void UserTimelineHandler::Reset() {
  fclose(ff);
  ff = fopen("/social-network-microservices/time_logs/user_log","w");
}

UserTimelineHandler::UserTimelineHandler(
    Redis *redis_pool, mongoc_client_pool_t *mongodb_pool,
    ClientPool<ThriftClient<PostStorageServiceClient>> *post_client_pool) {
  _redis_client_pool = redis_pool;
  _redis_cluster_client_pool = nullptr;
  _mongodb_client_pool = mongodb_pool;
  _post_client_pool = post_client_pool;
  //ff = fopen("/social-network-microservices/time_logs/user_log","w");
  
  cnt.store(0);
}

UserTimelineHandler::UserTimelineHandler(
    RedisCluster *redis_pool, mongoc_client_pool_t *mongodb_pool,
    ClientPool<ThriftClient<PostStorageServiceClient>> *post_client_pool) {
  _redis_cluster_client_pool = redis_pool;
  _redis_client_pool = nullptr;
  _mongodb_client_pool = mongodb_pool;
  _post_client_pool = post_client_pool;
  //ff = fopen("/social-network-microservices/log","w");
}

void UserTimelineHandler::WriteUserTimeline(
    const int8_t flow, const int64_t timestamp2, const int16_t level,
    int64_t req_id, int64_t post_id, int64_t user_id, int64_t timestamp,
    const std::map<std::string, std::string> &carrier) {
  //assert(ff != nullptr);
  // Initialize a span
  //TextMapReader reader(carrier);
  std::map<std::string, std::string> writer_text_map;
  //TextMapWriter writer(writer_text_map);
  //auto parent_span = opentracing::Tracer::Global()->Extract(reader);
  //auto span = opentracing::Tracer::Global()->StartSpan(
  //    "write_user_timeline_server", {opentracing::ChildOf(parent_span->get())});
  //opentracing::Tracer::Global()->Inject(span->context(), writer);

  mongoc_client_t *mongodb_client =
      mongoc_client_pool_pop(_mongodb_client_pool);
  if (!mongodb_client) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_MONGODB_ERROR;
    se.message = "Failed to pop a client from MongoDB pool";
    throw se;
  }
  auto collection = mongoc_client_get_collection(
      mongodb_client, "user-timeline", "user-timeline");
  if (!collection) {
    ServiceException se;
    se.errorCode = ErrorCode::SE_MONGODB_ERROR;
    se.message = "Failed to create collection user-timeline from MongoDB";
    mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
    throw se;
  }
  bson_t *query = bson_new();

  BSON_APPEND_INT64(query, "user_id", user_id);
  bson_t *update =
      BCON_NEW("$push", "{", "posts", "{", "$each", "[", "{", "post_id",
               BCON_INT64(post_id), "timestamp", BCON_INT64(timestamp), "}",
               "]", "$position", BCON_INT32(0), "}", "}");
  bson_error_t error;
  bson_t reply;
  //auto update_span = opentracing::Tracer::Global()->StartSpan(
  //    "write_user_timeline_mongo_insert_client",
  //    {opentracing::ChildOf(&span->context())});
  bool updated = mongoc_collection_find_and_modify(collection, query, nullptr,
                                                   update, nullptr, false, true,
                                                   true, &reply, &error);
  //update_span->Finish();

  if (!updated) {
    // update the newly inserted document (upsert: false)
    updated = mongoc_collection_find_and_modify(collection, query, nullptr,
                                                update, nullptr, false, false,
                                                true, &reply, &error);
    if (!updated) {
      LOG(error) << "Failed to update user-timeline for user " << user_id
                 << " to MongoDB: " << error.message;
      ServiceException se;
      se.errorCode = ErrorCode::SE_MONGODB_ERROR;
      se.message = error.message;
      bson_destroy(update);
      bson_destroy(query);
      bson_destroy(&reply);
      mongoc_collection_destroy(collection);
      mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
      throw se;
    }
  }

  bson_destroy(update);
  bson_destroy(&reply);
  bson_destroy(query);
  mongoc_collection_destroy(collection);
  mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);

  // Update user's timeline in redis
  //auto redis_span = opentracing::Tracer::Global()->StartSpan(
  //    "write_user_timeline_redis_update_client",
  //    {opentracing::ChildOf(&span->context())});
  try {
    if (_redis_client_pool)
      _redis_client_pool->zadd(std::to_string(user_id), std::to_string(post_id),
                              timestamp, UpdateType::NOT_EXIST);
    else
      _redis_cluster_client_pool->zadd(std::to_string(user_id), std::to_string(post_id),
                              timestamp, UpdateType::NOT_EXIST);

  } catch (const Error &err) {
    LOG(error) << err.what();
    throw err;
  }
  //redis_span->Finish();
  //span->Finish();
}

void UserTimelineHandler::ReadUserTimeline(
    std::vector<Post> &_return,
    const int8_t flow, const int64_t timestamp2, const int16_t level,
    int64_t req_id, int64_t user_id, int start,
    int stop, const std::map<std::string, std::string> &carrier) {
  // flow => level
  // level => sum_exec_metric_prev
  struct timespec tstart1;
  clock_gettime(CLOCK_REALTIME, &tstart1);
  auto t0 = high_resolution_clock::now();
  
  int64_t tpkt = (flow != 0) ? timestamp2 : tstart1.tv_sec*1000000000 + tstart1.tv_nsec;

  std::string time_str="";
  // Initialize a span
  std::map<std::string, std::string> writer_text_map;

  if (stop <= start || start < 0) {
    return;
  }

  auto t1 = high_resolution_clock::now();
  std::vector<std::string> post_ids_str;
  try {
    if (_redis_client_pool)
      _redis_client_pool->zrevrange(std::to_string(user_id), start, stop - 1,
                                  std::back_inserter(post_ids_str));
    else
      _redis_cluster_client_pool->zrevrange(std::to_string(user_id), start, stop - 1,
                                  std::back_inserter(post_ids_str));
  } catch (const Error &err) {
    LOG(error) << err.what();
    throw err;
  }
  auto t2 = high_resolution_clock::now();

  std::vector<int64_t> post_ids;
  for (auto &post_id_str : post_ids_str) {
    post_ids.emplace_back(std::stoul(post_id_str));
  }

  // find in mongodb
  bool mongodb_used = false;
  auto t3 = high_resolution_clock::now();
  int mongo_start = start + post_ids.size();
  std::unordered_map<std::string, double> redis_update_map;
  if (mongo_start < stop) {
    // Instead find post_ids from mongodb
    mongoc_client_t *mongodb_client =
        mongoc_client_pool_pop(_mongodb_client_pool);
    if (!mongodb_client) {
      ServiceException se;
      se.errorCode = ErrorCode::SE_MONGODB_ERROR;
      se.message = "Failed to pop a client from MongoDB pool";
      throw se;
    }
    mongodb_used = true;
    auto collection = mongoc_client_get_collection(
        mongodb_client, "user-timeline", "user-timeline");
    if (!collection) {
      ServiceException se;
      se.errorCode = ErrorCode::SE_MONGODB_ERROR;
      se.message = "Failed to create collection user-timeline from MongoDB";
      throw se;
    }

    bson_t *query = BCON_NEW("user_id", BCON_INT64(user_id));
    bson_t *opts = BCON_NEW("projection", "{", "posts", "{", "$slice", "[",
                            BCON_INT32(0), BCON_INT32(stop), "]", "}", "}");

    mongoc_cursor_t *cursor =
        mongoc_collection_find_with_opts(collection, query, opts, nullptr);
    const bson_t *doc;
    bool found = mongoc_cursor_next(cursor, &doc);
    if (found) {
      bson_iter_t iter_0;
      bson_iter_t iter_1;
      bson_iter_t post_id_child;
      bson_iter_t timestamp_child;
      int idx = 0;
      bson_iter_init(&iter_0, doc);
      bson_iter_init(&iter_1, doc);
      while (bson_iter_find_descendant(
                 &iter_0, ("posts." + std::to_string(idx) + ".post_id").c_str(),
                 &post_id_child) &&
             BSON_ITER_HOLDS_INT64(&post_id_child) &&
             bson_iter_find_descendant(
                 &iter_1,
                 ("posts." + std::to_string(idx) + ".timestamp").c_str(),
                 &timestamp_child) &&
             BSON_ITER_HOLDS_INT64(&timestamp_child)) {
        auto curr_post_id = bson_iter_int64(&post_id_child);
        auto curr_timestamp = bson_iter_int64(&timestamp_child);
        if (idx >= mongo_start) {
          //In mixed workload condition, post may composed between redis and mongo read
          //mongodb index will shift and duplicate post_id occurs
          if ( std::find(post_ids.begin(), post_ids.end(), curr_post_id) == post_ids.end() ) {
            post_ids.emplace_back(curr_post_id);
          }
        }
        redis_update_map.insert(std::make_pair(std::to_string(curr_post_id),
                                               (double)curr_timestamp));
        bson_iter_init(&iter_0, doc);
        bson_iter_init(&iter_1, doc);
        idx++;
      }
    }
    bson_destroy(opts);
    bson_destroy(query);
    mongoc_cursor_destroy(cursor);
    mongoc_collection_destroy(collection);
    mongoc_client_pool_push(_mongodb_client_pool, mongodb_client);
  }
  auto t4 = high_resolution_clock::now();
  uint16_t exec = (uint16_t)(duration_cast<microseconds>(t4-t1).count());

  std::future<std::vector<Post>> post_future =
      std::async(std::launch::async, [&]() {
        auto post_client_wrapper = _post_client_pool->Pop();
        if (!post_client_wrapper) {
          ServiceException se;
          se.errorCode = ErrorCode::SE_THRIFT_CONN_ERROR;
          se.message = "Failed to connect to post-storage-service";
          throw se;
        }
        std::vector<Post> _return_posts;
        auto post_client = post_client_wrapper->GetClient();
        try {
          post_client->ReadPosts(_return_posts, flow+3, tpkt, level+exec, req_id, post_ids,
                                 writer_text_map);
        } catch (...) {
          _post_client_pool->Remove(post_client_wrapper);
          LOG(error) << "Failed to read posts from post-storage-service";
          throw;
        }
        _post_client_pool->Keepalive(post_client_wrapper);
        return _return_posts;
      });

  if (redis_update_map.size() > 0) {
    try {
      if (_redis_client_pool)
        _redis_client_pool->zadd(std::to_string(user_id),
                               redis_update_map.begin(),
                               redis_update_map.end());
      else
        _redis_cluster_client_pool->zadd(std::to_string(user_id),
                               redis_update_map.begin(),
                               redis_update_map.end());

    } catch (const Error &err) {
      LOG(error) << err.what();
      throw err;
    }
  }
  auto t5 = high_resolution_clock::now();

  try {
    _return = post_future.get();
  } catch (...) {
    LOG(error) << "Failed to get post from post-storage-service";
    throw;
  }
  float e1 = duration_cast<microseconds>(t5-t0).count();
  float e2 = duration_cast<microseconds>(t2-t1).count();
  float e3 = duration_cast<microseconds>(t4-t3).count();
  float e4 = duration_cast<microseconds>(t5-t4).count();
  
  float i1 = duration_cast<microseconds>(t1-t0).count();
  float i2 = duration_cast<microseconds>(t3-t0).count();

  stats[0].exec_metric = 0.98*stats[0].exec_metric + 0.02*(e1-e2-e3-e4);
  stats[1].exec_metric = 0.98*stats[1].exec_metric + 0.02*e2;
  if(mongodb_used)
    stats[2].exec_metric = 0.98*stats[2].exec_metric + 0.02*e3;
    
  stats[0].exec_full = 0.98*stats[0].exec_full + 0.02*(e1-e2-e3); 
  stats[1].exec_full = stats[1].exec_metric;
  if(mongodb_used)
    stats[2].exec_full = stats[2].exec_metric;

  stats[0].upscale = -1;
  stats[1].upscale = 0.98*stats[1].upscale + 0.02*(i1-(e1-e2-e3-e4));
  if(mongodb_used)
     stats[2].upscale = 0.98*stats[2].upscale + 0.02*((i2-(e1-e3-e4))/2);
   
  //float mult = 0.02;
  //// Doing the thing below leads to unfairness, the first sample keeps a very high weight. But, it is needed because of lack of initialization.
  //if(stats[0].in_time.find(flow) == stats[0].in_time.end()) 
  //  mult = 1;
  //stats[0].in_time[flow] = 0;
  //stats[0].exec_sum_time[flow] = 0;

  //stats[1].in_time[flow+1] = (1-mult)*stats[1].in_time[flow+1] + mult*i1;
  //stats[2].in_time[flow+2] = (1-mult)*stats[2].in_time[flow+2] + mult*i2;

  //stats[1].exec_sum_time[flow+1] = (1-mult)*stats[1].exec_sum_time[flow+1] + mult*(e1-e2-e3-e4);
  //stats[2].exec_sum_time[flow+2] = (1-mult)*stats[2].exec_sum_time[flow+2] + mult*(e1-e3-e4);

  cnt++;
  if(cnt.load() >= 500) {
    mymutex.lock();
    if(cnt.load() >= 500) {
      std::system(("echo " + print_stat(stats[0]) + " > /social-network-microservices/shared/user-timeline-service").c_str());
      std::system(("echo " + print_stat(stats[1]) + " > /social-network-microservices/shared/user-timeline-redis").c_str());
      std::system(("echo " + print_stat(stats[2]) + " > /social-network-microservices/shared/user-timeline-mongodb").c_str());
      
      //std::string str = std::to_string(exec[0]) + " " + std::to_string(exec2[0]) + " " + std::to_string(in[0]);
      //std::system(("echo " + str + " > /social-network-microservices/shared/user-timeline-service").c_str());
      //
      //str = std::to_string(exec[1]) + " " + std::to_string(exec2[1]) + " " + std::to_string(in[1]);
      //std::system(("echo " + str + " > /social-network-microservices/shared/user-timeline-redis").c_str());
      //
      //str = std::to_string(exec[2]) + " " + std::to_string(exec2[2]) + " " + std::to_string(in[2]);
      //std::system(("echo " + str + " > /social-network-microservices/shared/user-timeline-mongodb").c_str());
      cnt.store(0);
    }
    mymutex.unlock();
  } 
  //auto te = high_resolution_clock::now();
}

}  // namespace social_network

#endif  // SOCIAL_NETWORK_MICROSERVICES_SRC_USERTIMELINESERVICE_USERTIMELINEHANDLER_H_
