find_package(OpenCV REQUIRED)
find_package(TIFF REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

find_package(nlohmann_json 3 QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(json)
endif()
