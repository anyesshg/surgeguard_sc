package frontend

import (
	"encoding/json"
	"fmt"
	"net/http"
	"strconv"
        "time"
        "os"
	"sync"
	recommendation "github.com/harlow/go-micro-services/services/recommendation/proto"
	reservation "github.com/harlow/go-micro-services/services/reservation/proto"
	user "github.com/harlow/go-micro-services/services/user/proto"
	"github.com/rs/zerolog/log"

	"github.com/harlow/go-micro-services/dialer"
	"github.com/harlow/go-micro-services/registry"
	profile "github.com/harlow/go-micro-services/services/profile/proto"
	search "github.com/harlow/go-micro-services/services/search/proto"
	"github.com/harlow/go-micro-services/tls"
	//"github.com/harlow/go-micro-services/tracing"
	//"github.com/opentracing/opentracing-go"
)

// Server implements frontend service
type Server struct {
	searchClient         search.SearchClient
	profileClient        profile.ProfileClient
	recommendationClient recommendation.RecommendationClient
	userClient           user.UserClient
	reservationClient    reservation.ReservationClient
	IpAddr               string
	Port                 int
	//Tracer               opentracing.Tracer
	Registry             *registry.Client

       // Extra variables for timing
       recoReq               int
       searchReq             int
       recoIn[2]             float32
       recoExec[2]           float32

       // Mutexes
       mu1 		sync.Mutex
       mu2		sync.Mutex
}

// Run the server
func (s *Server) Run() error {
	if s.Port == 0 {
		return fmt.Errorf("Server port must be set")
	}

	log.Info().Msg("Initializing gRPC clients...")
	if err := s.initSearchClient("search:8082"); err != nil {
		return err
	}

	if err := s.initProfileClient("profile:8081"); err != nil {
		return err
	}

	if err := s.initRecommendationClient("recommendation:8085"); err != nil {
		return err
	}

	if err := s.initUserClient("user:8086"); err != nil {
		return err
	}

	if err := s.initReservation("reservation:8087"); err != nil {
		return err
	}
	log.Info().Msg("Successfull")

	//log.Trace().Msg("frontend before mux")
	//mux := tracing.NewServeMux(s.Tracer)
	mux := http.NewServeMux()
	mux.Handle("/", http.FileServer(http.Dir("services/frontend/static")))
	mux.Handle("/hotels", http.HandlerFunc(s.searchHandler))
	mux.Handle("/recommendations", http.HandlerFunc(s.recommendHandler))
	mux.Handle("/user", http.HandlerFunc(s.userHandler))
	mux.Handle("/reservation", http.HandlerFunc(s.reservationHandler))

	log.Trace().Msg("frontend starts serving")
        s.recoReq = 0
        s.searchReq = 0
        s.recoIn[0] = 0.0
        s.recoIn[1] = 0.0
        s.recoExec[0] = 0.0
        s.recoExec[1] = 0.0

	tlsconfig := tls.GetHttpsOpt()
	srv := &http.Server{
		Addr:    fmt.Sprintf(":%d", s.Port),
		Handler: mux,
	}
	if tlsconfig != nil {
		log.Info().Msg("Serving https")
		srv.TLSConfig = tlsconfig
		return srv.ListenAndServeTLS("x509/server_cert.pem", "x509/server_key.pem")
	} else {
		log.Info().Msg("Serving https")
		return srv.ListenAndServe()
	}
}

func (s *Server) initSearchClient(name string) error {
	conn, err := dialer.Dial(
		name,
		//dialer.WithTracer(s.Tracer),
		//dialer.WithBalancer(s.Registry.Client),
	)
	if err != nil {
		return fmt.Errorf("dialer error: %v", err)
	}
	s.searchClient = search.NewSearchClient(conn)
	return nil
}

func (s *Server) initProfileClient(name string) error {
	conn, err := dialer.Dial(
		name,
		//dialer.WithTracer(s.Tracer),
		//dialer.WithBalancer(s.Registry.Client),
	)
	if err != nil {
		return fmt.Errorf("dialer error: %v", err)
	}
	s.profileClient = profile.NewProfileClient(conn)
	return nil
}

func (s *Server) initRecommendationClient(name string) error {
	conn, err := dialer.Dial(
		name,
		//dialer.WithTracer(s.Tracer),
		//dialer.WithBalancer(s.Registry.Client),
	)
	if err != nil {
		return fmt.Errorf("dialer error: %v", err)
	}
	s.recommendationClient = recommendation.NewRecommendationClient(conn)
	return nil
}

func (s *Server) initUserClient(name string) error {
	conn, err := dialer.Dial(
		name,
		//dialer.WithTracer(s.Tracer),
		//dialer.WithBalancer(s.Registry.Client),
	)
	if err != nil {
		return fmt.Errorf("dialer error: %v", err)
	}
	s.userClient = user.NewUserClient(conn)
	return nil
}

func (s *Server) initReservation(name string) error {
	conn, err := dialer.Dial(
		name,
		//dialer.WithTracer(s.Tracer),
		//dialer.WithBalancer(s.Registry.Client),
	)
	if err != nil {
		return fmt.Errorf("dialer error: %v", err)
	}
	s.reservationClient = reservation.NewReservationClient(conn)
	return nil
}

func (s *Server) searchHandler(w http.ResponseWriter, r *http.Request) {
	t1 := time.Now()
        w.Header().Set("Access-Control-Allow-Origin", "*")
	ctx := r.Context()
        //fmt.Println("searchHandler called inside frontend.")

	//log.Trace().Msg("starts searchHandler")

	// in/out dates from query params
	inDate, outDate := r.URL.Query().Get("inDate"), r.URL.Query().Get("outDate")
        // fmt.Println("Got URL query")
	if inDate == "" || outDate == "" {
	//	fmt.Println("Please specify inDate/outDate params", http.StatusBadRequest)
		http.Error(w, "Please specify inDate/outDate params", http.StatusBadRequest)
		return
	}

	// lan/lon from query params
        // fmt.Println("Got URL query")
	sLat, sLon := r.URL.Query().Get("lat"), r.URL.Query().Get("lon")
	if sLat == "" || sLon == "" {
	//	fmt.Println("Please specify location params", http.StatusBadRequest)
		http.Error(w, "Please specify location params", http.StatusBadRequest)
		return
	}
        // fmt.Println("Got URL query")

	Lat, _ := strconv.ParseFloat(sLat, 32)
	lat := float32(Lat)
	Lon, _ := strconv.ParseFloat(sLon, 32)
	lon := float32(Lon)

	//log.Trace().Msg("starts searchHandler querying downstream")

	//log.Info().Msgf(" SEARCH [lat: %v, lon: %v, inDate: %v, outDate: %v", lat, lon, inDate, outDate)
	// search for best hotels
        t2 := time.Now()
	//conn, err := dialer.Dial("search:8082",)
	//if err != nil {
	//	fmt.Errorf("dialer error: %v", err)
	//	return
	//}
	//searchClient := search.NewSearchClient(conn)
	//searchResp, err := searchClient.Nearby(ctx, &search.NearbyRequest{
	searchResp, err := s.searchClient.Nearby(ctx, &search.NearbyRequest{
		Lat:     lat,
		Lon:     lon,
		InDate:  inDate,
		OutDate: outDate,
                Timestamp2: t1.UnixNano(),
	})
        t3 := time.Now()
	//log.Info().Msgf("SearchHandler gets searchResp with error ", err)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	//fmt.Println("SearchHandler gets searchResp")
	//log.Info().Msg("SearchHandler gets searchResp")
	//for _, hid := range searchResp.HotelIds {
	//	fmt.Println("Search Handler hotelId = %s", hid)
	//	log.Info().Msgf("Search Handler hotelId = %s", hid)
	//}

	// grab locale from query params or default to en
	locale := r.URL.Query().Get("locale")
	if locale == "" {
		locale = "en"
	}

        t4 := time.Now()
	reservationResp, err := s.reservationClient.CheckAvailability(ctx, &reservation.Request{
		CustomerName: "",
		HotelId:      searchResp.HotelIds,
		InDate:       inDate,
		OutDate:      outDate,
		RoomNumber:   1,
                Timestamp2:   t1.UnixNano(),
	})
        t5 := time.Now()
	if err != nil {
	//	fmt.Println("SearchHandler CheckAvailability failed")
		log.Error().Msg("SearchHandler CheckAvailability failed")
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	//fmt.Println("searchHandler gets reserveResp")
	//log.Info().Msgf("searchHandler gets reserveResp.HotelId = %s", reservationResp.HotelId)

	// hotel profiles
        t6 := time.Now()
	profileResp, err := s.profileClient.GetProfiles(ctx, &profile.Request{
		HotelIds: reservationResp.HotelId,
		Locale:   locale,
                Timestamp2:   t1.UnixNano(),
	})
        t7 := time.Now()
	if err != nil {
	//	fmt.Println("SearchHandler GetProfiles failed")
		log.Error().Msg("SearchHandler GetProfiles failed")
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	//fmt.Println("searchHandler gets profileResp")
	// log.Info().Msg("searchHandler gets profileResp")

	json.NewEncoder(w).Encode(geoJSONResponse(profileResp.Hotels))
        t8 := time.Now()
        
        this_exec := float32(t8.UnixNano()-t1.UnixNano()-(t3.UnixNano()-t2.UnixNano())-(t5.UnixNano()-t4.UnixNano())-(t7.UnixNano()-t6.UnixNano()))/float32(1000000.0)
        s.recoExec[0] = float32(0.99)*s.recoExec[0] + float32(0.01)*this_exec
        s.searchReq = s.searchReq + 1
        if s.searchReq >= 500 {
	  s.mu1.Lock() 
	  if s.searchReq >= 500 {
            f1, err1 := os.Create("/stats/frontend")
            //f1, err1 := os.OpenFile("/stats/frontend", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
            if err1 != nil {
	      return
            }
            defer f1.Close()
            f1.WriteString(fmt.Sprintf("%f %f %f %f\n", s.recoIn[0], s.recoIn[0], s.recoExec[0], s.recoExec[0]))
            f1.Sync()
            s.searchReq = 0
	  }
	  s.mu1.Unlock()
        }
}

func (s *Server) recommendHandler(w http.ResponseWriter, r *http.Request) {
        t1 := time.Now()
	w.Header().Set("Access-Control-Allow-Origin", "*")
	ctx := r.Context()

	sLat, sLon := r.URL.Query().Get("lat"), r.URL.Query().Get("lon")
	if sLat == "" || sLon == "" {
		http.Error(w, "Please specify location params", http.StatusBadRequest)
		return
	}
	Lat, _ := strconv.ParseFloat(sLat, 64)
	lat := float64(Lat)
	Lon, _ := strconv.ParseFloat(sLon, 64)
	lon := float64(Lon)

	require := r.URL.Query().Get("require")
	if require != "dis" && require != "rate" && require != "price" {
		http.Error(w, "Please specify require params", http.StatusBadRequest)
		return
	}

	// recommend hotels
        t2 := time.Now()
	recResp, err := s.recommendationClient.GetRecommendations(ctx, &recommendation.Request{
		Require: require,
		Lat:     float64(lat),
		Lon:     float64(lon),
                Timestamp2:   t1.UnixNano(),
	})
        t3 := time.Now()
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	// grab locale from query params or default to en
	locale := r.URL.Query().Get("locale")
	if locale == "" {
		locale = "en"
	}

	// hotel profiles
        t4 := time.Now()
	profileResp, err := s.profileClient.GetProfiles(ctx, &profile.Request{
		HotelIds: recResp.HotelIds,
		Locale:   locale,
                Timestamp2:   t1.UnixNano(),
	})
        t5 := time.Now()
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	json.NewEncoder(w).Encode(geoJSONResponse(profileResp.Hotels))
        t6 := time.Now()
	
        // 0 - this, 1 - recommend
        this_exec := float32(t6.UnixNano()-t1.UnixNano()-(t3.UnixNano()-t2.UnixNano())-(t5.UnixNano()-t4.UnixNano()))/float32(1000000.0)
        reco_exec := float32(t3.UnixNano()-t2.UnixNano())/float32(1000000.0)
        s.recoIn[1] = float32(0.99)*s.recoIn[1] + float32(0.01)*float32(t2.UnixNano()-t1.UnixNano())/float32(1000000.0)

        s.recoExec[0] = float32(0.99)*s.recoExec[0] + float32(0.01)*this_exec
        s.recoExec[1] = float32(0.99)*s.recoExec[1] + float32(0.01)*reco_exec
        s.recoReq = s.recoReq + 1
        if s.recoReq >= 500 { 
	  s.mu2.Lock()
	  if s.recoReq >= 500 {
            f1, err1 := os.Create("/stats/frontend")
            //f1, err1 := os.OpenFile("/stats/frontend", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
            if err1 != nil {
	      return
            }
            defer f1.Close()
            f2, err2 := os.Create("/stats/recommendation")
            //f2, err2 := os.OpenFile("/stats/recommendation", os.O_APPEND|os.O_WRONLY|os.O_CREATE, 0666)
            if err2 != nil {
	      return
            }
            defer f2.Close()
            f1.WriteString(fmt.Sprintf("%f %f %f %f\n", s.recoIn[0], s.recoIn[0], s.recoExec[0], s.recoExec[0]))
            f2.WriteString(fmt.Sprintf("%f %f %f %f\n", s.recoIn[1], s.recoIn[1], s.recoExec[1], s.recoExec[1]))
            f1.Sync()
            f2.Sync()
            s.recoReq = 0
	  }
	  s.mu2.Unlock()
        }
}

func (s *Server) userHandler(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Access-Control-Allow-Origin", "*")
	ctx := r.Context()

	username, password := r.URL.Query().Get("username"), r.URL.Query().Get("password")
	if username == "" || password == "" {
		http.Error(w, "Please specify username and password", http.StatusBadRequest)
		return
	}

	// Check username and password
	recResp, err := s.userClient.CheckUser(ctx, &user.Request{
		Username: username,
		Password: password,
	})
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	str := "Login successfully!"
	if recResp.Correct == false {
		str = "Failed. Please check your username and password. "
	}

	res := map[string]interface{}{
		"message": str,
	}

	json.NewEncoder(w).Encode(res)
}

func (s *Server) reservationHandler(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Access-Control-Allow-Origin", "*")
	ctx := r.Context()

	inDate, outDate := r.URL.Query().Get("inDate"), r.URL.Query().Get("outDate")
	if inDate == "" || outDate == "" {
		http.Error(w, "Please specify inDate/outDate params", http.StatusBadRequest)
		return
	}

	if !checkDataFormat(inDate) || !checkDataFormat(outDate) {
		http.Error(w, "Please check inDate/outDate format (YYYY-MM-DD)", http.StatusBadRequest)
		return
	}

	hotelId := r.URL.Query().Get("hotelId")
	if hotelId == "" {
		http.Error(w, "Please specify hotelId params", http.StatusBadRequest)
		return
	}

	customerName := r.URL.Query().Get("customerName")
	if customerName == "" {
		http.Error(w, "Please specify customerName params", http.StatusBadRequest)
		return
	}

	username, password := r.URL.Query().Get("username"), r.URL.Query().Get("password")
	if username == "" || password == "" {
		http.Error(w, "Please specify username and password", http.StatusBadRequest)
		return
	}

	numberOfRoom := 0
	num := r.URL.Query().Get("number")
	if num != "" {
		numberOfRoom, _ = strconv.Atoi(num)
	}

	// Check username and password
	recResp, err := s.userClient.CheckUser(ctx, &user.Request{
		Username: username,
		Password: password,
	})
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	str := "Reserve successfully!"
	if recResp.Correct == false {
		str = "Failed. Please check your username and password. "
	}

	// Make reservation
	resResp, err := s.reservationClient.MakeReservation(ctx, &reservation.Request{
		CustomerName: customerName,
		HotelId:      []string{hotelId},
		InDate:       inDate,
		OutDate:      outDate,
		RoomNumber:   int32(numberOfRoom),
	})
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	if len(resResp.HotelId) == 0 {
		str = "Failed. Already reserved. "
	}

	res := map[string]interface{}{
		"message": str,
	}

	json.NewEncoder(w).Encode(res)
}

// return a geoJSON response that allows google map to plot points directly on map
// https://developers.google.com/maps/documentation/javascript/datalayer#sample_geojson
func geoJSONResponse(hs []*profile.Hotel) map[string]interface{} {
	fs := []interface{}{}

	for _, h := range hs {
		fs = append(fs, map[string]interface{}{
			"type": "Feature",
			"id":   h.Id,
			"properties": map[string]string{
				"name":         h.Name,
				"phone_number": h.PhoneNumber,
			},
			"geometry": map[string]interface{}{
				"type": "Point",
				"coordinates": []float32{
					h.Address.Lon,
					h.Address.Lat,
				},
			},
		})
	}

	return map[string]interface{}{
		"type":     "FeatureCollection",
		"features": fs,
	}
}

func checkDataFormat(date string) bool {
	if len(date) != 10 {
		return false
	}
	for i := 0; i < 10; i++ {
		if i == 4 || i == 7 {
			if date[i] != '-' {
				return false
			}
		} else {
			if date[i] < '0' || date[i] > '9' {
				return false
			}
		}
	}
	return true
}
