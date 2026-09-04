cmake_minimum_required(VERSION 3.16)

project(FileXORProcessor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Core)

set(SOURCES
    main.cpp
    mainwindow.cpp
    fileprocessor.cpp
)

set(HEADERS
    mainwindow.h
    fileprocessor.h
)

set(FORMS
    mainwindow.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(FileXORProcessor
        MANUAL_FINALIZATION
        ${SOURCES}
        ${HEADERS}
        ${FORMS}
    )

    target_link_libraries(FileXORProcessor PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Core
    )

    qt_finalize_executable(FileXORProcessor)
else()
    add_executable(FileXORProcessor
        ${SOURCES}
        ${HEADERS}
        ${FORMS}
    )

    target_link_libraries(FileXORProcessor PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Core
    )
endif()

if(WIN32)
    set_target_properties(FileXORProcessor PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()
