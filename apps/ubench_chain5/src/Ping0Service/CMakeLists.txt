add_executable(
    Ping0Service
    Ping0Service.cpp
    ${THRIFT_GEN_CPP_DIR}/social_network_types.cpp
    ${THRIFT_GEN_CPP_DIR}/Ping0Service.cpp
    ${THRIFT_GEN_CPP_DIR}/Ping1Service.cpp
)

target_include_directories(
    Ping0Service PRIVATE
    /usr/local/include/jaegertracing
)

target_link_libraries(
    Ping0Service
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

install(TARGETS Ping0Service DESTINATION ./)
