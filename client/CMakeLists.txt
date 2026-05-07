cmake_minimum_required(VERSION 3.24)

set(CMAKE_MFC_FLAG 2)  # Dynamic MFC (must be before project())

project(StudySyncClient VERSION 0.1.0 LANGUAGES CXX)

option(STUDYSYNC_ENABLE_LOCAL_MEDIAPIPE "Build local MediaPipe analyzer adapter" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(StudySyncClient WIN32
    src/app/pch.cpp
    src/app/StudySyncClientApp.cpp
    src/app/MainFrm.cpp
    src/app/StudySyncClientView.cpp
    src/app/LoginDlg.cpp
    src/app/RegisterDlg.cpp
    src/capture/CaptureThread.cpp
    src/core/WorkerThreadPool.cpp
    src/event/EventShadowBuffer.cpp
    src/event/PostureEventDetector.cpp
    src/render/D2DRenderer.cpp
    src/render/RenderThread.cpp
    src/render/OverlayPainter.cpp
    src/network/ConnectionState.cpp
    src/network/AiTcpClient.cpp
    src/network/EventUploadThread.cpp
    src/network/HeartbeatClient.cpp
    src/network/ClientTransportConfig.cpp
    src/network/ClientTransportFactory.cpp
    src/network/HttpJsonlLogSink.cpp
    src/network/JsonlBatchUploader.cpp
    src/network/LocalClaimCheckClipStore.cpp
    src/network/LocalClipGarbageCollector.cpp
    src/network/ReconnectPolicy.cpp
    src/network/WinHttpClient.cpp
    src/network/AuthApi.cpp
    src/network/TokenStore.cpp
    src/network/SessionApi.cpp
    src/network/StatsApi.cpp
    src/alert/AlertManager.cpp
    src/alert/AlertDispatchThread.cpp
    src/analysis/DummyAnalysisGenerator.cpp
    src/analysis/ZmqPoseAnalyzer.cpp
    src/analysis/LocalMediaPipePoseAnalyzer.cpp
    res/StudySyncClient.rc
)

target_include_directories(StudySyncClient PRIVATE
    include
    src/app
    res
)

target_compile_definitions(StudySyncClient PRIVATE
    UNICODE
    _UNICODE
    _AFXDLL
)

target_precompile_headers(StudySyncClient PRIVATE src/app/pch.h)

if(MSVC)
    set_property(TARGET StudySyncClient PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    set_target_properties(StudySyncClient PROPERTIES
        VS_GLOBAL_UseOfMfc "Dynamic"
    )

    target_compile_options(StudySyncClient PRIVATE /permissive- /W4 /utf-8)
endif()

find_package(OpenCV CONFIG QUIET)
if(NOT OpenCV_FOUND AND EXISTS "C:/opencv/build/OpenCVConfig.cmake")
    set(OpenCV_DIR "C:/opencv/build" CACHE PATH "OpenCV config directory" FORCE)
    find_package(OpenCV CONFIG QUIET)
endif()

if(OpenCV_FOUND)
    target_link_libraries(StudySyncClient PRIVATE ${OpenCV_LIBS})
    target_compile_definitions(StudySyncClient PRIVATE STUDYSYNC_HAS_OPENCV=1)
else()
    message(WARNING "OpenCV was not found. Set OpenCV_DIR before building the capture pipeline.")
endif()

target_link_libraries(StudySyncClient PRIVATE d2d1 dxgi dwrite winhttp ws2_32)

if(STUDYSYNC_ENABLE_LOCAL_MEDIAPIPE)
    target_compile_definitions(StudySyncClient PRIVATE STUDYSYNC_ENABLE_LOCAL_MEDIAPIPE=1)
    message(WARNING "Local MediaPipe adapter is a placeholder until the Windows C++ dependency strategy is finalized.")
endif()
