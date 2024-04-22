package reservation

import (
	"encoding/json"
	"fmt"
	"sync"
	"github.com/google/uuid"
	//"github.com/grpc-ecosystem/grpc-opentracing/go/otgrpc"
	"github.com/harlow/go-micro-services/registry"
	pb "github.com/harlow/go-micro-services/services/reservation/proto"
	"github.com/harlow/go-micro-services/tls"
	//"github.com/opentracing/opentracing-go"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/keepalive"
	"gopkg.in/mgo.v2"
	"gopkg.in/mgo.v2/bson"

	"io/ioutil"
	"net"
	"os"
	"time"

	"github.com/bradfitz/gomemcache/memcache"
	"github.com/rs/zerolog/log"

	// "strings"
	"strconv"
)

const name = "srv-reservation"

// Server implements the user service
type Server struct {
	//Tracer       opentracing.Tracer
	Port         int
	IpAddr       string
	MongoSession *mgo.Session
	Registry     *registry.Client
	MemcClient   *memcache.Client
	uuid         string

        // extra variables for timing
        req         int
        exec[3]     float32
        in[3]       float32
	mu1	    sync.Mutex
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

	pb.RegisterReservationServer(srv, s)

	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", s.Port))
	if err != nil {
		log.Fatal().Msgf("failed to listen: %v", err)
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

	log.Trace().Msgf("In reservation s.IpAddr = %s, port = %d", s.IpAddr, s.Port)

	//err = s.Registry.Register(name, s.uuid, s.IpAddr, s.Port)
	//if err != nil {
	//	return fmt.Errorf("failed register: %v", err)
	//}
	//log.Info().Msg("Successfully registered in consul")
	s.req = 0
	s.exec[0] = 0
	s.exec[1] = 0
	s.exec[2] = 0
	s.in[0] = 0
	s.in[1] = 0
	s.in[2] = 0
	return srv.Serve(lis)
}

// Shutdown cleans up any processes
func (s *Server) Shutdown() {
	s.Registry.Deregister(s.uuid)
}

// MakeReservation makes a reservation based on given information
func (s *Server) MakeReservation(ctx context.Context, req *pb.Request) (*pb.Result, error) {
	res := new(pb.Result)
	res.HotelId = make([]string, 0)

	// session, err := mgo.Dial("mongodb-reservation")
	// if err != nil {
	// 	panic(err)
	// }
	// defer session.Close()
	session := s.MongoSession.Copy()
	defer session.Close()

	c := session.DB("reservation-db").C("reservation")
	c1 := session.DB("reservation-db").C("number")

	inDate, _ := time.Parse(
		time.RFC3339,
		req.InDate+"T12:00:00+00:00")

	outDate, _ := time.Parse(
		time.RFC3339,
		req.OutDate+"T12:00:00+00:00")
	hotelId := req.HotelId[0]

	indate := inDate.String()[0:10]

	memc_date_num_map := make(map[string]int)

	for inDate.Before(outDate) {
		// check reservations
		count := 0
		inDate = inDate.AddDate(0, 0, 1)
		outdate := inDate.String()[0:10]

		// first check memc
		memc_key := hotelId + "_" + inDate.String()[0:10] + "_" + outdate
		item, err := s.MemcClient.Get(memc_key)
		if err == nil {
			// memcached hit
			count, _ = strconv.Atoi(string(item.Value))
			//log.Trace().Msgf("memcached hit %s = %d", memc_key, count)
			memc_date_num_map[memc_key] = count + int(req.RoomNumber)

		} else if err == memcache.ErrCacheMiss {
			// memcached miss
			//log.Trace().Msgf("memcached miss")
			reserve := make([]reservation, 0)
			err := c.Find(&bson.M{"hotelId": hotelId, "inDate": indate, "outDate": outdate}).All(&reserve)
			if err != nil {
				log.Panic().Msgf("Tried to find hotelId [%v] from date [%v] to date [%v], but got error", hotelId, indate, outdate, err.Error())
			}

			for _, r := range reserve {
				count += r.Number
			}

			memc_date_num_map[memc_key] = count + int(req.RoomNumber)

		} else {
			log.Panic().Msgf("Tried to get memc_key [%v], but got memmcached error = %s", memc_key, err)
		}

		// check capacity
		// check memc capacity
		memc_cap_key := hotelId + "_cap"
		item, err = s.MemcClient.Get(memc_cap_key)
		hotel_cap := 0
		if err == nil {
			// memcached hit
			hotel_cap, _ = strconv.Atoi(string(item.Value))
			//log.Trace().Msgf("memcached hit %s = %d", memc_cap_key, hotel_cap)
		} else if err == memcache.ErrCacheMiss {
			// memcached miss
			var num number
			err = c1.Find(&bson.M{"hotelId": hotelId}).One(&num)
			if err != nil {
				log.Panic().Msgf("Tried to find hotelId [%v], but got error", hotelId, err.Error())
			}
			hotel_cap = int(num.Number)

			// write to memcache
			s.MemcClient.Set(&memcache.Item{Key: memc_cap_key, Value: []byte(strconv.Itoa(hotel_cap))})
		} else {
			log.Panic().Msgf("Tried to get memc_cap_key [%v], but got memmcached error = %s", memc_cap_key, err)
		}

		if count+int(req.RoomNumber) > hotel_cap {
			return res, nil
		}
		indate = outdate
	}

	// only update reservation number cache after check succeeds
	for key, val := range memc_date_num_map {
		s.MemcClient.Set(&memcache.Item{Key: key, Value: []byte(strconv.Itoa(val))})
	}

	inDate, _ = time.Parse(
		time.RFC3339,
		req.InDate+"T12:00:00+00:00")

	indate = inDate.String()[0:10]

	for inDate.Before(outDate) {
		inDate = inDate.AddDate(0, 0, 1)
		outdate := inDate.String()[0:10]
		err := c.Insert(&reservation{
			HotelId:      hotelId,
			CustomerName: req.CustomerName,
			InDate:       indate,
			OutDate:      outdate,
			Number:       int(req.RoomNumber)})
		if err != nil {
			log.Panic().Msgf("Tried to insert hotel [hotelId %v], but got error", hotelId, err.Error())
		}
		indate = outdate
	}

	res.HotelId = append(res.HotelId, hotelId)

	return res, nil
}

// CheckAvailability checks if given information is available
func (s *Server) CheckAvailability(ctx context.Context, req *pb.Request) (*pb.Result, error) {
	t1 := time.Now()
	t2 := time.Now()
	t3 := time.Now()

        this_mc := int64(0)
        this_mongo := int64(0)
        this_mc_req := 0
        this_mongo_req := 0

	extra_mc := int64(0)
	extra_mongo := int64(0)
	extra_mc_req := 0
	extra_mongo_req := 0

        res := new(pb.Result)
	res.HotelId = make([]string, 0)

	// session, err := mgo.Dial("mongodb-reservation")
	// if err != nil {
	// 	panic(err)
	// }
	// defer session.Close()
	session := s.MongoSession.Copy()
	defer session.Close()

	c := session.DB("reservation-db").C("reservation")
	c1 := session.DB("reservation-db").C("number")

	for _, hotelId := range req.HotelId {
		//log.Trace().Msgf("reservation check hotel %s", hotelId)
		inDate, _ := time.Parse(
			time.RFC3339,
			req.InDate+"T12:00:00+00:00")

		outDate, _ := time.Parse(
			time.RFC3339,
			req.OutDate+"T12:00:00+00:00")

		indate := inDate.String()[0:10]

		for inDate.Before(outDate) {
			// check reservations
			count := 0
			inDate = inDate.AddDate(0, 0, 1)
			//log.Trace().Msgf("reservation check date %s", inDate.String()[0:10])
			outdate := inDate.String()[0:10]

			// first check memc
			memc_key := hotelId + "_" + inDate.String()[0:10] + "_" + outdate
			t2 = time.Now()
			item, err := s.MemcClient.Get(memc_key)
			t3 = time.Now()

			if err == nil {
				// memcached hit
				this_mc = this_mc + (t3.UnixNano()-t2.UnixNano())
				this_mc_req = this_mc_req + 1
				
				count, _ = strconv.Atoi(string(item.Value))
				//log.Trace().Msgf("memcached hit %s = %d", memc_key, count)
			} else if err == memcache.ErrCacheMiss {
				// memcached miss
				reserve := make([]reservation, 0)

				t2 = time.Now()
				err := c.Find(&bson.M{"hotelId": hotelId, "inDate": indate, "outDate": outdate}).All(&reserve)
				t3 = time.Now()
				this_mongo = this_mongo + (t3.UnixNano()-t2.UnixNano())
				this_mongo_req = this_mongo_req + 1

				
				if err != nil {
					log.Panic().Msgf("Tried to find hotelId [%v] from date [%v] to date [%v], but got error", hotelId, indate, outdate, err.Error())
				}
				for _, r := range reserve {
					//log.Trace().Msgf("reservation check reservation number = %d", hotelId)
					count += r.Number
				}

				// update memcachedi
				t2 = time.Now()
				s.MemcClient.Set(&memcache.Item{Key: memc_key, Value: []byte(strconv.Itoa(count))})
				t3 = time.Now()
				this_mc = this_mc + (t3.UnixNano()-t2.UnixNano())
				this_mc_req = this_mc_req + 1
			} else {
				log.Panic().Msgf("Tried to get memc_key [%v], but got memmcached error = %s", memc_key, err)

			}

			// check capacity
			// check memc capacity
			memc_cap_key := hotelId + "_cap"
			t2 = time.Now()
			item, err = s.MemcClient.Get(memc_cap_key)
			t3 = time.Now()
			
			hotel_cap := 0

			if err == nil {
				// memcached hit
				hotel_cap, _ = strconv.Atoi(string(item.Value))
				extra_mc = extra_mc + (t3.UnixNano()-t2.UnixNano())
				extra_mc_req = extra_mc_req + 1
				//log.Trace().Msgf("memcached hit %s = %d", memc_cap_key, hotel_cap)
			} else if err == memcache.ErrCacheMiss {
				var num number
				t2 = time.Now()
				err = c1.Find(&bson.M{"hotelId": hotelId}).One(&num)
				t3 = time.Now()
				extra_mongo = extra_mongo + (t3.UnixNano()-t2.UnixNano())
				extra_mongo_req = extra_mongo_req + 1
				
				if err != nil {
					log.Panic().Msgf("Tried to find hotelId [%v], but got error", hotelId, err.Error())
				}
				hotel_cap = int(num.Number)
				// update memcached
				t2 = time.Now()
				s.MemcClient.Set(&memcache.Item{Key: memc_cap_key, Value: []byte(strconv.Itoa(hotel_cap))})
				t3 = time.Now()
				extra_mc = extra_mc + (t3.UnixNano()-t2.UnixNano())
				extra_mc_req = extra_mc_req + 1
			} else {
				log.Panic().Msgf("Tried to get memc_key [%v], but got memmcached error = %s", memc_cap_key, err)
			}

			if count+int(req.RoomNumber) > hotel_cap {
				break
			}
			indate = outdate

			if inDate.Equal(outDate) {
				res.HotelId = append(res.HotelId, hotelId)
			}
		}
	}
	t4 := time.Now()
	s.req = s.req + 1
	//mc_exec := (float32(this_mc + extra_mc)/float32(this_mc_req+extra_mc_req))/float32(1000000.0)
	//mongo_exec := (float32(this_mongo + extra_mongo)/float32(this_mongo_req+extra_mongo_req))/float32(1000000.0)

	res_exec := float32(t4.UnixNano() - t1.UnixNano() - this_mc - extra_mc - this_mongo - extra_mongo)/float32(1000000.0)

	in_time := float32(t1.UnixNano()-req.Timestamp2)/float32(1000000.0)
	
	if (this_mc_req + extra_mc_req != 0) {
	    mc_exec := (float32(this_mc + extra_mc)/float32(this_mc_req+extra_mc_req))/float32(1000000.0)
	    s.exec[1] = 0.99*s.exec[1] + 0.01*mc_exec
	}

	if this_mongo_req + extra_mongo_req != 0 {
	    mongo_exec := (float32(this_mongo + extra_mongo)/float32(this_mongo_req+extra_mongo_req))/float32(1000000.0)
	    s.exec[2] = 0.99*s.exec[2] + 0.01*mongo_exec
	}

	s.in[0] = 0.99*s.in[0] + 0.01*in_time
	s.exec[0] = 0.99*s.exec[0] + 0.01*res_exec

	if(s.req >= 500) {
	    s.mu1.Lock()
	    if s.req >= 500 {
	      f1, err1 := os.Create("/stats/reservation")
	      //f1, err1 := os.OpenFile("/stats/reservation", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
	      if err1 != nil {
		  return res, nil
	      }
	      defer f1.Close()
	      f2, err2 := os.Create("/stats/memcached-reserve")
	      //f2, err2 := os.OpenFile("/stats/memcached-reserve", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
	      if err2 != nil {
		  return res, nil
	      }
	      defer f2.Close()
	      f3, err3 := os.Create("/stats/mongodb-reservation")
	      //f3, err3 := os.OpenFile("/stats/mongodb-reservation", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
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
	return res, nil
}

type reservation struct {
	HotelId      string `bson:"hotelId"`
	CustomerName string `bson:"customerName"`
	InDate       string `bson:"inDate"`
	OutDate      string `bson:"outDate"`
	Number       int    `bson:"number"`
}

type number struct {
	HotelId string `bson:"hotelId"`
	Number  int    `bson:"numberOfRoom"`
}
