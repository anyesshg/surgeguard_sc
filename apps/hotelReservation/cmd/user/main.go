package main

import (
	"encoding/json"
	"flag"
	"io/ioutil"
	"os"
	"strconv"
	"time"

//	"github.com/harlow/go-micro-services/registry"
	"github.com/harlow/go-micro-services/services/user"
//	"github.com/harlow/go-micro-services/tracing"
	"github.com/rs/zerolog"
	"github.com/rs/zerolog/log"
)

func main() {
	// initializeDatabase()
	log.Logger = zerolog.New(zerolog.ConsoleWriter{Out: os.Stdout, TimeFormat: time.RFC3339}).With().Timestamp().Caller().Logger()

	log.Info().Msg("Reading config...")
	jsonFile, err := os.Open("config.json")
	if err != nil {
		log.Error().Msgf("Got error while reading config: %v", err)
	}

	defer jsonFile.Close()

	byteValue, _ := ioutil.ReadAll(jsonFile)

	var result map[string]string
	json.Unmarshal([]byte(byteValue), &result)

	log.Info().Msgf("Read database URL: %v", result["UserMongoAddress"])
	log.Info().Msg("Initializing DB connection...")
	mongo_session := initializeDatabase(result["UserMongoAddress"])
	defer mongo_session.Close()
	log.Info().Msg("Successfull")

	serv_port, _ := strconv.Atoi(result["UserPort"])
	serv_ip := result["UserIP"]
	log.Info().Msgf("Read target port: %v", serv_port)
	log.Info().Msgf("Read consul address: %v", result["consulAddress"])
//	log.Info().Msgf("Read jaeger address: %v", result["jaegerAddress"])

//	var (
		// port       = flag.Int("port", 8086, "The server port")
//		jaegeraddr = flag.String("jaegeraddr", result["jaegerAddress"], "Jaeger server addr")
//		consuladdr = flag.String("consuladdr", result["consulAddress"], "Consul address")
//	)
	flag.Parse()

//	log.Info().Msgf("Initializing jaeger agent [service name: %v | host: %v]...", "user", *jaegeraddr)
//	tracer, err := tracing.Init("user", *jaegeraddr)
//	if err != nil {
//		log.Panic().Msgf("Got error while initializing jaeger agent: %v", err)
//	}
//	log.Info().Msg("Jaeger agent initialized")

//	log.Info().Msgf("Initializing consul agent [host: %v]...", *consuladdr)
//	registry, err := registry.NewClient(*consuladdr)
//	if err != nil {
//		log.Panic().Msgf("Got error while initializing consul agent: %v", err)
//	}
//	log.Info().Msg("Consul agent initialized")

	srv := &user.Server{
		//Tracer: tracer,
		// Port:     *port,
//		Registry:     registry,
		Port:         serv_port,
		IpAddr:       serv_ip,
		MongoSession: mongo_session,
	}

	log.Info().Msg("Starting server...")
	log.Fatal().Msg(srv.Run().Error())
}
