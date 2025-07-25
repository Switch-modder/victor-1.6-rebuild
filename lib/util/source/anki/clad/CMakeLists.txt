include(generate_clad)
include(anki_build_strip)

set(CLAD_BASE_DIR "${CMAKE_SOURCE_DIR}/tools/message-buffers")
set(CLAD_EMITTER_DIR "${CLAD_BASE_DIR}/emitters")
set(CLAD_CPP "${CLAD_EMITTER_DIR}/CPP_emitter.py")
set(CLAD_HASH "${CLAD_EMITTER_DIR}/ASTHash_emitter.py")

#CLAD_CPP_DECL=../robot/clad/cozmo_CPP_declarations_emitter.py
#CLAD_CPP_SWITCH=./cozmo_CPP_switch_emitter.py
#CLAD_HASH=$(CLAD_EMITTER_DIR)/ASTHash_emitter.py
set(CLAD_CPP_FLAGS "--output-properties")

set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/generated/clad/util")
set(PY_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/generated/cladPython")

set(UTIL_SRCS
    util/ankiLab/ankiLabDef.clad
)

generate_clad_cpp(
    SRCS ${UTIL_SRCS}
    FLAGS ${CLAD_CPP_FLAGS}
    RELATIVE_SRC_DIR "."
    OUTPUT_DIR "${OUTPUT_DIR}"
    OUTPUT_SRCS CLAD_CPP_GEN_UTIL_SRCS
)

add_library(util_clad STATIC
    ${CLAD_CPP_GEN_UTIL_SRCS}
    ${CLAD_PY_GEN_UTIL_SRCS}
)

target_include_directories(util_clad
    PUBLIC
    $<BUILD_INTERFACE:${OUTPUT_DIR}>
)

target_compile_definitions(util_clad
  PUBLIC
  USES_CLAD
)

target_link_libraries(util_clad
    clad
)

anki_build_target_license(util_clad "ANKI")

anki_build_strip(TARGET util_clad)

generate_clad_py(
    TARGET "util_clad_python"
    SRCS ${UTIL_SRCS}
    RELATIVE_SRC_DIR "."
    OUTPUT_DIR "${PY_OUTPUT_DIR}"
    OUTPUT_SRCS CLAD_PY_GEN_UTIL_SRCS
)
