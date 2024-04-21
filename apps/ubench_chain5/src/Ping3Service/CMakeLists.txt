add_executable(
    Ping3Service
    Ping3Service.cpp
    ${THRIFT_GEN_CPP_DIR}/social_network_types.cpp
    ${THRIFT_GEN_CPP_DIR}/Ping3Service.cpp
    ${THRIFT_GEN_CPP_DIR}/Ping4Service.cpp
)

target_include_directories(
    Ping3Service PRIVATE
    /usr/local/include/jaegertracing
)

SET(GCC_LINKER_FLAGS "-lrt")
SET(CMAKE_EXEC_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${GCC_LINKER_FLAGS}")

target_link_libraries(
    Ping3Service
    ${THRIFT_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
    rt
    ${Boost_LIBRARIES}
    nlohmann_json::nlohmann_json
    Boost::log
    Boost::log_setup
    OpenSSL::SSL
    /usr/local/lib/libjaegertracing.so
    /usr/local/lib/libSimpleAmqpClient.so

)

install(TARGETS Ping3Service DESTINATION ./)
