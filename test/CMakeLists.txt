cmake_minimum_required(VERSION 3.8)

project(dsmr_test)

add_executable(dsmr_test
	main.cpp
	../parser.c
	../parser.h
	../dsmr.h
)

target_include_directories(dsmr_test PRIVATE ../)
target_compile_features(dsmr_test PRIVATE c_std_99 cxx_std_14)
