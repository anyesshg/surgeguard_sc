package search

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net"
	"github.com/rs/zerolog/log"

	"os"
	"time"
	"sync"

	"github.com/google/uuid"
	//"github.com/grpc-ecosystem/grpc-opentracing/go/otgrpc"
	"github.com/harlow/go-micro-services/dialer"
	"github.com/harlow/go-micro-services/registry"
	geo "github.com/harlow/go-micro-services/services/geo/proto"
	rate "github.com/harlow/go-micro-services/services/rate/proto"
	pb "github.com/harlow/go-micro-services/services/search/proto"
	"github.com/harlow/go-micro-services/tls"
	//opentracing "github.com/opentracing/opentracing-go"
	context "golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/keepalive"
)

const name = "srv-search"

// Server implments the search service
type Server struct {
	geoClient  geo.GeoClient
	rateClient rate.RateClient

	//Tracer   opentracing.Tracer
	Port     int
	IpAddr   string
	Registry *registry.Client
	uuid     string

       // Extra variables for timing
       request   float32
       start[3]  float32
       exec[3]   float32
       mu        sync.Mutex
}

// Run starts the server
func (s *Server) Run() error {
	if s.Port == 0 {
		return fmt.Errorf("server port must be set")
	}

	s.uuid = uuid.New().String()

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
	pb.RegisterSearchServer(srv, s)

	// init grpc clients
	if err := s.initGeoClient("geo:8083"); err != nil {
		return err
	}
	if err := s.initRateClient("rate:8084"); err != nil {
		return err
	}

	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", s.Port))
	if err != nil {
		log.Fatal().Msgf("failed to listen: %v", err)
	}

	// register with consul
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
        s.request = 0
        s.start[0] = 0.0
        s.start[1] = 0.0
        s.start[2] = 0.0
        s.exec[0] = 0.0
        s.exec[1] = 0.0
        s.exec[2] = 0.0 
	return srv.Serve(lis)
}

// Shutdown cleans up any processes
func (s *Server) Shutdown() {
	s.Registry.Deregister(s.uuid)
}

func (s *Server) initGeoClient(name string) error {
	conn, err := dialer.Dial(
		name,
		//dialer.WithTracer(s.Tracer),
		//dialer.WithBalancer(s.Registry.Client),
	)
	if err != nil {
		return fmt.Errorf("dialer error: %v", err)
	}
	s.geoClient = geo.NewGeoClient(conn)
	return nil
}

func (s *Server) initRateClient(name string) error {
	conn, err := dialer.Dial(
		name,
		//dialer.WithTracer(s.Tracer),
		//dialer.WithBalancer(s.Registry.Client),
	)
	if err != nil {
		return fmt.Errorf("dialer error: %v", err)
	}
	s.rateClient = rate.NewRateClient(conn)
	return nil
}

func (s *Server) foo() {
   f1, err1 := os.Create("/stats/search")
   //f1, err1 := os.OpenFile("/stats/search", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
   if err1 == nil {
     f1.WriteString(fmt.Sprintf("%f %f %f %f\n", s.start[0]/s.request, s.start[0]/s.request, s.exec[0]/s.request, s.exec[0]/s.request))
   }
   defer f1.Close()
   f1.Sync()
   
   f2, err2 := os.Create("/stats/geo")
   //f2, err2 := os.OpenFile("/stats/geo", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
   if err2 == nil {
     f2.WriteString(fmt.Sprintf("%f %f %f %f\n", s.start[1]/s.request, s.start[1]/s.request, s.exec[1]/s.request, s.exec[1]/s.request))
   }
   defer f2.Close()
   f2.Sync()
   
   f3, err3 := os.Create("/stats/rate")
   //f3, err3 := os.OpenFile("/stats/rate", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
   if err3 == nil {
     f3.WriteString(fmt.Sprintf("%f %f %f %f\n", s.start[2]/s.request, s.start[2]/s.request, s.exec[2]/s.request, s.exec[2]/s.request))
   }
   defer f3.Close()
   f3.Sync()
   s.request = 0
}

// Nearby returns ids of nearby hotels ordered by ranking algo
func (s *Server) Nearby(ctx context.Context, req *pb.NearbyRequest) (*pb.SearchResult, error) {
	// find nearby hotels
	//fmt.Println("in Search Nearby")

	//log.Trace().Msgf("nearby lat = %f", req.Lat)
	//log.Trace().Msgf("nearby lon = %f", req.Lon)
        t1 := time.Now()
	nearby, err := s.geoClient.Nearby(ctx, &geo.Request{
		Lat: req.Lat,
		Lon: req.Lon,
                Timestamp2: req.Timestamp2,
	})
        t2 := time.Now()
	if err != nil {
		return nil, err
	}

	//for _, hid := range nearby.HotelIds {
	//	log.Trace().Msgf("get Nearby hotelId = %s", hid)
	//}

	// find rates for hotels
        t3 := time.Now()
	rates, err := s.rateClient.GetRates(ctx, &rate.Request{
		HotelIds: nearby.HotelIds,
		InDate:   req.InDate,
		OutDate:  req.OutDate,
                Timestamp2: req.Timestamp2,
	})
        t4 := time.Now()
	if err != nil {
		return nil, err
	}

	// TODO(hw): add simple ranking algo to order hotel ids:
	// * geo distance
	// * price (best discount?)
	// * reviews

	// build the response
	res := new(pb.SearchResult)
	for _, ratePlan := range rates.RatePlans {
		//log.Trace().Msgf("get RatePlan HotelId = %s, Code = %s", ratePlan.HotelId, ratePlan.Code)
		res.HotelIds = append(res.HotelIds, ratePlan.HotelId)
	}
        t5 := time.Now()
        this_exec := float32(t5.UnixNano() - t1.UnixNano() - (t2.UnixNano() - t1.UnixNano()) - (t4.UnixNano() - t3.UnixNano()))/float32(1000000.0)
        geo_exec := float32(t2.UnixNano() - t1.UnixNano())/float32(1000000.0)
        rate_exec := float32(t4.UnixNano() - t3.UnixNano())/float32(1000000.0)

	// 0 - this, 1 - geo, 2 - rate
       s.start[0] = float32(0.95)*s.start[0] + float32(0.05)*float32(t1.UnixNano()-req.Timestamp2)/float32(1000000.0)
       s.start[1] = float32(0.95)*s.start[1] + float32(0.05)*float32(t1.UnixNano()-req.Timestamp2)/float32(1000000.0)
       s.start[2] = float32(0.95)*s.start[2] + float32(0.05)*float32(t3.UnixNano()-req.Timestamp2)/float32(1000000.0)

       s.exec[0] = float32(0.95)*s.exec[0] + float32(0.05)*(this_exec)
       s.exec[1] = float32(0.95)*s.exec[1] + float32(0.05)*geo_exec
       s.exec[2] = float32(0.95)*s.exec[2] + float32(0.05)*rate_exec
        
	//s.start[0] = float32(0.99)*s.start[0] + float32(0.01)*float32(t1.UnixNano()-req.Timestamp2)/float32(1000000.0)
        //s.start[1] = float32(0.99)*s.start[1] + float32(0.01)*float32(t1.UnixNano()-req.Timestamp2)/float32(1000000.0)
        //s.start[2] = float32(0.99)*s.start[2] + float32(0.01)*float32(t3.UnixNano()-req.Timestamp2)/float32(1000000.0)

        //s.exec[0] = s.exec[0]*float32(0.99) + float32(0.01)*(this_exec)
        //s.exec[1] = s.exec[1]*float32(0.99) + float32(0.01)*geo_exec
        //s.exec[2] = s.exec[2]*float32(0.99) + float32(0.01)*rate_exec
        s.request = s.request + 1
        if s.request >= 200 {
	  s.mu.Lock()
	  if s.request >= 200 {
	    s.foo()
	  }
	  s.mu.Unlock()
          //f1, err1 := os.Create("/stats/search")
          ////f1, err1 := os.OpenFile("/stats/search", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
          //if err1 != nil {
	  //  return res, nil
          //}
          //defer f1.Close()
          //f2, err2 := os.Create("/stats/geo")
          ////f2, err2 := os.OpenFile("/stats/geo", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
          //if err2 != nil {
	  //  return res, nil
          //}
          //defer f2.Close()
          //f3, err3 := os.Create("/stats/rate")
          ////f3, err3 := os.OpenFile("/stats/rate", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
          //if err3 != nil {
	  //  return res, nil
          //}
          //defer f3.Close()
          //f1.WriteString(fmt.Sprintf("%f %f %f %f\n", s.start[0], s.start[0], s.exec[0], s.exec[0]))
          //f2.WriteString(fmt.Sprintf("%f %f %f %f\n", s.start[1], s.start[1], s.exec[1], s.exec[1]))
          //f3.WriteString(fmt.Sprintf("%f %f %f %f\n", s.start[2], s.start[2], s.exec[2], s.exec[2]))
          //f1.Sync()
          //f2.Sync()
          //f3.Sync()
          //s.request = 0
        }
	return res, nil
}
