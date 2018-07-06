project(compressor)

add_library(${PROJECT_NAME}
	src/compressor.cpp
	src/BitVector.cpp
)

target_include_directories(${PROJECT_NAME}
	PUBLIC
		${PROJECT_SOURCE_DIR}/include
	PRIVATE
		${PROJECT_SOURCE_DIR}/src
)
