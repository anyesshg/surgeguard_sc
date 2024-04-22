package profile

import (
	"encoding/json"
	"fmt"
	"sync"
	"gopkg.in/mgo.v2"
	"gopkg.in/mgo.v2/bson"

	"io/ioutil"
	"net"
	"os"
	"time"

	"github.com/rs/zerolog/log"

	"github.com/google/uuid"
	//"github.com/grpc-ecosystem/grpc-opentracing/go/otgrpc"
	"github.com/harlow/go-micro-services/registry"
	pb "github.com/harlow/go-micro-services/services/profile/proto"
	"github.com/harlow/go-micro-services/tls"
	//"github.com/opentracing/opentracing-go"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/keepalive"

	"github.com/bradfitz/gomemcache/memcache"
	// "strings"
)

const name = "srv-profile"

// Server implements the profile service
type Server struct {
	//Tracer       opentracing.Tracer
	uuid         string
	Port         int
	IpAddr       string
	MongoSession *mgo.Session
	Registry     *registry.Client
	MemcClient   *memcache.Client

	// Extras
	in[3]	    float32
	exec[3]     float32
	req	    int

	// Mutexas
	mu1 	    sync.Mutex
}

// Run starts the server
func (s *Server) Run() error {
	if s.Port == 0 {
		return fmt.Errorf("server port must be set")
	}

	s.uuid = uuid.New().String()

	log.Trace().Msgf("in run s.IpAddr = %s, port = %d", s.IpAddr, s.Port)

	opts := []grpc.ServerOption{
		grpc.KeepaliveParams(keepalive.ServerParameters{
			Timeout: 120 * time.Second,
		}),
		grpc.KeepaliveEnforcementPolicy(keepalive.EnforcementPolicy{
			PermitWithoutStream: true,
		}),
		//grpc.UnaryInterceptor(
		//	otgrpc.OpenTracingServerInterceptor(s.Tracer),
		//),
	}

	if tlsopt := tls.GetServerOpt(); tlsopt != nil {
		opts = append(opts, tlsopt)
	}

	srv := grpc.NewServer(opts...)

	pb.RegisterProfileServer(srv, s)

	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", s.Port))
	if err != nil {
		log.Fatal().Msgf("failed to configure listener: %v", err)
	}

	// register the service
	jsonFile, err := os.Open("config.json")
	if err != nil {
		fmt.Println(err)
	}

	defer jsonFile.Close()

	byteValue, _ := ioutil.ReadAll(jsonFile)

	var result map[string]string
	json.Unmarshal([]byte(byteValue), &result)

	//err = s.Registry.Register(name, s.uuid, s.IpAddr, s.Port)
	//if err != nil {
	//	return fmt.Errorf("failed register: %v", err)
	//}
	//log.Info().Msg("Successfully registered in consul")
	s.req = 0
	s.in[0] = 0
	s.in[1] = 0
	s.in[2] = 0
	s.exec[0] = 0
	s.exec[1] = 0
	s.exec[2] = 0
	return srv.Serve(lis)
}

// Shutdown cleans up any processes
func (s *Server) Shutdown() {
	s.Registry.Deregister(s.uuid)
}

// GetProfiles returns hotel profiles for requested IDs
func (s *Server) GetProfiles(ctx context.Context, req *pb.Request) (*pb.Result, error) {
	t1 := time.Now()
	t2 := time.Now()
	t3 := time.Now()

	this_mc := int64(0)
	this_mongo := int64(0)
	this_mc_req := 0
	this_mongo_req := 0
	// session, err := mgo.Dial("mongodb-profile")
	// if err != nil {
	// 	panic(err)
	// }
	// defer session.Close()

	//log.Trace().Msgf("In GetProfiles")
	res := new(pb.Result)
	hotels := make([]*pb.Hotel, 0)

	// one hotel should only have one profile

	for _, i := range req.HotelIds {
		// first check memcached
		t2 = time.Now()
		item, err := s.MemcClient.Get(i)
		t3 = time.Now()
		if err == nil {
			// memcached hit
			profile_str := string(item.Value)
			log.Info().Msgf("memc hit with %v", profile_str)
			this_mc = this_mc + (t3.UnixNano() - t2.UnixNano())
			this_mc_req = this_mc_req + 1

			hotel_prof := new(pb.Hotel)
			json.Unmarshal(item.Value, hotel_prof)
			hotels = append(hotels, hotel_prof)

		} else if err == memcache.ErrCacheMiss {
			// memcached miss, set up mongo connection
			session := s.MongoSession.Copy()
			defer session.Close()
			c := session.DB("profile-db").C("hotels")

			hotel_prof := new(pb.Hotel)
			
			t2 = time.Now()
			err := c.Find(bson.M{"id": i}).One(&hotel_prof)
			t3 = time.Now()
			this_mongo = this_mongo + (t3.UnixNano() - t2.UnixNano())
			this_mongo_req = this_mongo_req + 1

			if err != nil {
				log.Error().Msgf("Failed get hotels data: ", err)
			}

			// for _, h := range hotels {
			// 	res.Hotels = append(res.Hotels, h)
			// }
			hotels = append(hotels, hotel_prof)

			prof_json, err := json.Marshal(hotel_prof)
			if err != nil {
				log.Error().Msgf("Failed to marshal hotel [id: %v] with err:", hotel_prof.Id, err)
			}
			memc_str := string(prof_json)

			// write to memcached
			t2 = time.Now()
			s.MemcClient.Set(&memcache.Item{Key: i, Value: []byte(memc_str)})
			t3 = time.Now()
			this_mc = this_mc + (t3.UnixNano() - t2.UnixNano())
			this_mc_req = this_mc_req + 1

		} else {
			log.Panic().Msgf("Tried to get hotelId [%v], but got memmcached error = %s", i, err)
		}
	}

	res.Hotels = hotels

	t4 := time.Now()
	s.req = s.req + 1
	mc_exec := (float32(this_mc)/float32(this_mc_req))/float32(1000000.0)
	mongo_exec := (float32(this_mongo)/float32(this_mongo_req))/float32(1000000.0)
	prof_exec := float32(t4.UnixNano() - t1.UnixNano() - this_mc - this_mongo)/float32(1000000.0)

	in_time := float32(t1.UnixNano() - req.Timestamp2)/float32(1000000.0)
	if (this_mc_req != 0 || this_mongo_req != 0) {
	    s.exec[1] = 0.99*s.exec[1] + 0.01*(mc_exec+mongo_exec)
	    //prof_exec += mc_exec
        }
	if this_mongo_req != 0 {
	    s.exec[2] = 0.99*s.exec[2] + 0.01*mongo_exec
	    //prof_exec += mongo_exec
	}
	s.in[0] = 0.99*s.in[0] + 0.01*in_time
	s.exec[0] = 0.99*s.exec[0] + 0.01*prof_exec

	if s.req >= 500 {
	    s.mu1.Lock()
	    if s.req >= 500 {
	      f1, err1 := os.Create("/stats/profile")
	      //f1, err1 := os.OpenFile("/stats/profile", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
	      if err1 != nil {
		  return res, nil
	      }
	      defer f1.Close()
	      f2, err2 := os.Create("/stats/memcached-profile")
	      //f2, err2 := os.OpenFile("/stats/memcached-profile", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
	      if err2 != nil {
		  return res, nil
	      }
	      defer f2.Close()
	      f3, err3 := os.Create("/stats/mongodb-profile")
	      //f3, err3 := os.OpenFile("/stats/mongodb-profile", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
	      if err3 != nil {
		  return res, nil
	      }
	      defer f3.Close()
	      f1.WriteString(fmt.Sprintf("%f %f %f %f\n",s.in[0], s.in[0], s.exec[0], s.exec[0]))
	      f2.WriteString(fmt.Sprintf("%f %f %f %f\n",s.in[0], s.in[0], s.exec[1], s.exec[1]))
	      f3.WriteString(fmt.Sprintf("%f %f %f %f\n",s.in[0], s.in[0], s.exec[2], s.exec[2]))
	      f1.Sync()
	      f2.Sync()
	      f3.Sync()
	      s.req = 0
	    }
	    s.mu1.Unlock()
	}
	//log.Trace().Msgf("In GetProfiles after getting resp")
	return res, nil
}
