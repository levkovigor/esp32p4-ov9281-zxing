file(GLOB_RECURSE SRCS "*.cpp" "*.c")

idf_component_register(SRCS ${SRCS}
                    INCLUDE_DIRS "include")
                    
target_compile_options(${COMPONENT_LIB} PRIVATE 
    -O3
    -Wno-stringop-overflow
    -Wno-array-bounds
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20 -fexceptions -frtti -Wno-error=type-limits>
    $<$<COMPILE_LANGUAGE:C>:-Wno-error=type-limits>)
