add_library(am_amcom STATIC)
target_compile_features(am_amcom PUBLIC c_std_11)

target_sources(
  am_amcom
  PRIVATE amcom.c
  PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/amcom.h)
