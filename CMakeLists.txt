cmake_minimum_required(VERSION 3.20)
project(War3ModelBatchViewer VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找Qt6组件
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    OpenGLWidgets
    Concurrent
)

# Qt编译设置
qt_standard_project_setup()

# 资源文件
qt_add_resources(RESOURCES resources.qrc)

# 源文件列表（假设源文件位于src/目录下，根据实际情况调整）
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/modelparser.cpp
    src/modelviewer.cpp
    src/settingsdialog.cpp
)

# 头文件列表
set(HEADERS
    src/mainwindow.h
    src/modelparser.h
    src/modelviewer.h
    src/settingsdialog.h
)

# 生成可执行文件
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# 复制 service 目录到可执行文件输出目录
set(SERVICE_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/service)
set(SERVICE_DST_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/service)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SERVICE_SRC_DIR} ${SERVICE_DST_DIR}
    COMMENT "Copying Node.js service to binary directory"
)

# 链接Qt库
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Concurrent
    Qt6::Network
)

# 平台特定的OpenGL库链接
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL32)
elseif(UNIX)
    find_package(OpenGL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL)
endif()

# Windows特有的设置
if(WIN32)
    # Debug模式显示控制台，Release模式隐藏
    set_target_properties(${PROJECT_NAME} PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
    )

    # 仅在Release模式下设置WIN32_EXECUTABLE
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            WIN32_EXECUTABLE TRUE
        )
    endif()
endif()

# 输出目录设置
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
