cmake_minimum_required(VERSION 3.15)
project(http_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS chrono)
find_package(rabbitmq-c REQUIRED)
find_package(libmysqlclient REQUIRED)
find_package(muduo REQUIRED)
find_package(SimpleAmqpClient REQUIRED)
find_package(mysql-connector-cpp-jdbc REQUIRED)
find_package(OpenCV QUIET)
find_package(onnxruntime QUIET)

file(GLOB_RECURSE HTTP_SERVER_SRC
    "${PROJECT_SOURCE_DIR}/HttpServer/src/*.cpp"
)

file(GLOB_RECURSE CHAT_SERVER_SRC
    "${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/*.cpp"
)

if(NOT OpenCV_FOUND OR NOT onnxruntime_FOUND)
    message(STATUS "OpenCV/onnxruntime not found; image recognition uses DashScope VL. "
                    "Optional local ONNX: conan install . -o with_opencv=True -o with_onnxruntime=True --build=missing")
endif()

set(MAIN_SRC "${PROJECT_SOURCE_DIR}/AIApps/ChatServer/src/main.cpp")

add_executable(http_server
    ${MAIN_SRC}
    ${HTTP_SERVER_SRC}
    ${CHAT_SERVER_SRC}
)

target_include_directories(http_server PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/HttpServer/include
    ${PROJECT_SOURCE_DIR}/AIApps/ChatServer/include
)

target_link_libraries(http_server PRIVATE
    pthread
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    nlohmann_json::nlohmann_json
    Boost::chrono
    muduo::muduo
    SimpleAmqpClient::SimpleAmqpClient
    mysql::jdbc
    libmysqlclient::libmysqlclient
)

if(TARGET rabbitmq::rabbitmq)
    target_link_libraries(http_server PRIVATE rabbitmq::rabbitmq)
elseif(TARGET rabbitmq-c::rabbitmq)
    target_link_libraries(http_server PRIVATE rabbitmq-c::rabbitmq)
elseif(TARGET rabbitmq::rabbitmq-static)
    target_link_libraries(http_server PRIVATE rabbitmq::rabbitmq-static)
endif()

if(OpenCV_FOUND AND onnxruntime_FOUND)
    target_link_libraries(http_server PRIVATE opencv::opencv onnxruntime::onnxruntime)
    target_compile_definitions(http_server PRIVATE CPPAI_WITH_VISION=1)
endif()

message(STATUS "Main source file: ${MAIN_SRC}")
