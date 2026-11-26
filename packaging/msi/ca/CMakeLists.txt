# packaging/msi/ca - the MSI's one custom action, a native DLL that checks the
# folders picked on the Games page (see check_folders.cpp). Built by
# build_msi.ps1 (MSVC, x64). Static CRT so the DLL needs no VC++ runtime on a
# machine that has none at Setup time.
cmake_minimum_required(VERSION 3.20)
project(mxbmrp3_msi_ca CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(mxbmrp3_msi_ca SHARED check_folders.cpp check_folders.def)
target_compile_options(mxbmrp3_msi_ca PRIVATE /W4 /WX /permissive-)
target_compile_definitions(mxbmrp3_msi_ca PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
target_link_libraries(mxbmrp3_msi_ca PRIVATE msi)
