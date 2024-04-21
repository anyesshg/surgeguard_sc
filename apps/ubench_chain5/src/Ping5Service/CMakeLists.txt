add_executable(
    Ping5Service
    Ping5Service.cpp
    ${THRIFT_GEN_CPP_DIR}/social_network_types.cpp
    ${THRIFT_GEN_CPP_DIR}/Ping5Service.cpp
    ${THRIFT_GEN_CPP_DIR}/Ping0Service.cpp
)

target_include_directories(
    Ping5Service PRIVATE
    /usr/local/include/jaegertracing
)

target_link_libraries(
    Ping5Service
    ${THRIFT_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_LIBRARIES}
    nlohmann_json::nlohmann_json
    Boost::log
    rt
    Boost::log_setup
    OpenSSL::SSL
    /usr/local/lib/libjaegertracing.so
    /usr/local/lib/libSimpleAmqpClient.so

)

install(TARGETS Ping5Service DESTINATION ./)
