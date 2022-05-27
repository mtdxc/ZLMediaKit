find_path(SRT_INCLUDE_DIR
        NAMES srt/srt.h
        HINTS ${SRT_PATH_ROOT}
        PATH_SUFFIXES include)

find_library(SRT_LIBRARY
        NAMES srt
        HINTS ${SRT_PATH_ROOT}
        PATH_SUFFIXES bin lib)

set(SRT_LIBRARIES ${SRT_LIBRARY})
set(SRT_INCLUDE_DIRS ${SRT_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(SRT DEFAULT_MSG SRT_LIBRARIES SRT_INCLUDE_DIR)
