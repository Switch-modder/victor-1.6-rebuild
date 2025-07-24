set(SIGNALESSENCE_EXTRA_INCLUDE_DIR "")
set(SIGNALESSENCE_LIB_PATH "")
set(SIGNALESSENCE_PLATFORM_DIR "")

if (VICOS)
  set(SIGNALESSENCE_PLATFORM_DIR "vicos")
  set(SIGNALESSENCE_EXTRA_INCLUDE_DIR "cpu_arm")
elseif (MACOSX)
  set(SIGNALESSENCE_PLATFORM_DIR "mac")
  set(SIGNALESSENCE_EXTRA_INCLUDE_DIR "cpu_none")
endif()

# Signal Essence Lib Version
set(SIGNALESSENCE_VERSION_PATH "v008") # Updated prototype version with Echo Cancelation

message(STATUS "Signal Essence lib ${SIGNALESSENCE_VERSION_PATH}")

set(SIGNALESSENCE_HOME "${ANKI_EXTERNAL_DIR}/anki-thirdparty/signalEssence/${SIGNALESSENCE_VERSION_PATH}")

set(SIGNALESSENCE_LIB_PATH "${SIGNALESSENCE_HOME}/${SIGNALESSENCE_PLATFORM_DIR}/platform/anki_victor_example/build")

set(SIGNALESSENCE_PROJ_SRC "${SIGNALESSENCE_HOME}/${SIGNALESSENCE_PLATFORM_DIR}/project/anki_victor")
set(SIGNALESSENCE_VAD_SRC "${SIGNALESSENCE_HOME}/${SIGNALESSENCE_PLATFORM_DIR}/project/anki_victor_vad")

set(SIGNALESSENCE_INCLUDE_PATHS
  "${SIGNALESSENCE_PROJ_SRC}"
  "${SIGNALESSENCE_VAD_SRC}"
  "${SIGNALESSENCE_HOME}/${SIGNALESSENCE_PLATFORM_DIR}/se_lib_public"
  "${SIGNALESSENCE_HOME}/${SIGNALESSENCE_PLATFORM_DIR}/se_lib_public/${SIGNALESSENCE_EXTRA_INCLUDE_DIR}"
)

set(SIGNALESSENCE_LIBS
  mmfx
)

if (VICOS)
  foreach(LIB ${SIGNALESSENCE_LIBS})
      add_library(${LIB} STATIC IMPORTED)
      set_target_properties(${LIB} PROPERTIES
          IMPORTED_LOCATION
          "${SIGNALESSENCE_LIB_PATH}/lib${LIB}.a"
          INTERFACE_INCLUDE_DIRECTORIES
          "${SIGNALESSENCE_INCLUDE_PATHS}")

      anki_build_target_license(${LIB} "Commercial")
  endforeach()
elseif (MACOSX)
  foreach(LIB ${SIGNALESSENCE_LIBS})
      add_library(${LIB} SHARED IMPORTED)
      set_target_properties(${LIB} PROPERTIES
          IMPORTED_LOCATION
          "${SIGNALESSENCE_LIB_PATH}/lib${LIB}.dylib"
          INTERFACE_INCLUDE_DIRECTORIES
          "${SIGNALESSENCE_INCLUDE_PATHS}")

      anki_build_target_license(${LIB} "Commercial")
  endforeach()
endif()
