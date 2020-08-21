CXX=g++
CXX_FLAGS=-std=c++20 -Wall -O0
INCLUDE_PATH=include

SRC_DIR=
BUILD_DIR=build
OBJ_FILES=bytes.o compile.o init.o main.o scan.o vm.o

all : fn

clean :
	rm -frv ${BUILD_DIR} fn

fn : $(addprefix ${BUILD_DIR}/,${OBJ_FILES})# ${MAIN_FILE}
	${CXX} ${CXX_FLAGS} \
		-I ${INCLUDE_PATH} \
		-o fn \
		$(addprefix ${BUILD_DIR}/,${OBJ_FILES}) 

${BUILD_DIR}/%.o : src/%.cpp $(wildcard include/%.hpp) | ${BUILD_DIR}
	${CXX} ${CXX_FLAGS} -c \
		-I ${INCLUDE_PATH} \
		-o build/$*.o \
		src/$*.cpp

${BUILD_DIR} :
	mkdir ${BUILD_DIR}
