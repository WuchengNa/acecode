cmake_minimum_required(VERSION 3.20)
set(VCPKG_MANIFEST_MODE OFF CACHE BOOL "" FORCE)
project(acecode VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force UTF-8 source and execution charset on MSVC
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(cpr CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(acecode 
    main.cpp
    src/config/config.cpp
    src/provider/openai_provider.cpp
    src/provider/copilot_provider.cpp
    src/provider/provider_factory.cpp
    src/auth/github_auth.cpp
    src/tool/tool_executor.cpp
    src/tool/bash_tool.cpp
    src/agent_loop.cpp
)

target_include_directories(acecode PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(acecode PRIVATE 
    cpr::cpr 
    ftxui::screen 
    ftxui::dom 
    ftxui::component
    nlohmann_json::nlohmann_json
)

