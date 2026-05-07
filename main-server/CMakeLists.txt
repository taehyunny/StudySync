# ============================================================================
# StudySync 메인서버 CMake 빌드
# ============================================================================
# 빌드:
#   cd main-server && mkdir build && cd build
#   cmake -DCMAKE_BUILD_TYPE=Release ..
#   make -j
#
# 실행:
#   ./studysync_main_server [config.json]
#
# 의존성:
#   - mariadb-client (시스템 패키지: libmariadb-dev)
#   - libcrypt (시스템: glibc 표준)
#   - cpp-httplib  (header-only, FetchContent 로 GitHub 에서 받음)
#   - jwt-cpp      (header-only, FetchContent)
#   - nlohmann_json (header-only, FetchContent)
# ============================================================================

cmake_minimum_required(VERSION 3.16)
project(studysync_main_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 컴파일 옵션
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# ── 시스템 의존성 ─────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(MARIADB REQUIRED libmariadb)

# bcrypt 는 glibc crypt_r 사용 → libcrypt 링크
find_library(CRYPT_LIB crypt REQUIRED)

# ── 외부 헤더 의존성 (FetchContent — 빌드 시 다운로드) ────────────
include(FetchContent)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.15.3
)
FetchContent_MakeAvailable(httplib)

FetchContent_Declare(
    jwt_cpp
    GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
    GIT_TAG        v0.7.0
)
set(JWT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(jwt_cpp)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# spdlog — logger.h 의 추가 백엔드 (error.log + rotating 백업)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)

# ── 소스 ──────────────────────────────────────────────────────────
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    src/*.cpp
)

add_executable(studysync_main_server ${SRC_FILES})

target_include_directories(studysync_main_server PRIVATE
    include
    common
    ${MARIADB_INCLUDE_DIRS}
)

target_link_libraries(studysync_main_server PRIVATE
    ${MARIADB_LIBRARIES}
    ${CRYPT_LIB}
    httplib::httplib
    jwt-cpp::jwt-cpp
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    pthread
)

target_link_directories(studysync_main_server PRIVATE
    ${MARIADB_LIBRARY_DIRS}
)

# 빌드 출력
message(STATUS "── StudySync main server ──")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  MariaDB libs : ${MARIADB_LIBRARIES}")
message(STATUS "  Sources      : ${SRC_FILES}")
