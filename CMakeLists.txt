cmake_minimum_required( VERSION 3.16.0 )

set( requires "Core2-for-AWS-IoT-Kit" )
set( include_dirs "./include" )

if( CONFIG_UNIT_VMETER_USE_PAHUB )
    list( APPEND requires "core2foraws-unit-pahub" )
    list( APPEND include_dirs "../core2foraws-unit-pahub/include" )
endif()

idf_component_register(
    SRCS "unit_vmeter.c"
    INCLUDE_DIRS ${include_dirs}
    REQUIRES ${requires}
)