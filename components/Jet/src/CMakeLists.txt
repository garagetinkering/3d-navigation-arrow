# Collect all .cpp files in the current directory
file(GLOB JET_SRC CONFIGURE_DEPENDS "*.cpp")

# Create a static library for Jet
add_library(Jet STATIC ${JET_SRC})

# Include headers for Jet
target_include_directories(Jet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- Documentation (Doxygen) ---------------------------------------------
# Adds an optional `jet_docs` target that runs Doxygen on the engine
# sources only. Skipped silently when Doxygen is not installed or when the
# engine is consumed as a sub-component (top-level project only).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR PROJECT_IS_TOP_LEVEL)
    find_package(Doxygen QUIET)
    if(DOXYGEN_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile")
        add_custom_target(jet_docs
            COMMAND ${DOXYGEN_EXECUTABLE} Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Generating Jet API documentation with Doxygen"
            VERBATIM)
    endif()
endif()
