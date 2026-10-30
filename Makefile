CC      := cc
CXX     := g++
CFLAGS  := -std=gnu11 -O2 -Wall -Wextra -Iinclude
CXXFLAGS := -std=gnu++17 -O2 -Wall -Wextra -Iinclude
LDFLAGS := -lm

JAVA_HOME ?= $(shell dirname $$(dirname $$(realpath $$(which javac))))
JNI_INC   := -I$(JAVA_HOME)/include -I$(JAVA_HOME)/include/linux

BIN_DIR := bin

.PHONY: all clean lib jni run run-v4l2 run-hal3 run-jni run-py run-all

all: $(BIN_DIR)/isp $(BIN_DIR)/v4l2 $(BIN_DIR)/hal3 lib

$(BIN_DIR)/isp.o: src/isp.c include/isp.h include/isp_macros.h include/alias.h
	mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -fPIC -c src/isp.c -o $(BIN_DIR)/isp.o

$(BIN_DIR)/isp: src/main.c $(BIN_DIR)/isp.o
	$(CC) $(CFLAGS) src/main.c $(BIN_DIR)/isp.o -o $(BIN_DIR)/isp $(LDFLAGS)

$(BIN_DIR)/v4l2: src/v4l2.c $(BIN_DIR)/isp.o
	$(CC) $(CFLAGS) src/v4l2.c $(BIN_DIR)/isp.o -o $(BIN_DIR)/v4l2 $(LDFLAGS)

$(BIN_DIR)/hal3: src/hal3.cpp $(BIN_DIR)/isp.o include/hal3.hpp
	$(CXX) $(CXXFLAGS) src/hal3.cpp $(BIN_DIR)/isp.o -o $(BIN_DIR)/hal3 $(LDFLAGS)

lib: $(BIN_DIR)/libisp.so

$(BIN_DIR)/libisp.so: $(BIN_DIR)/isp.o
	$(CC) $(CFLAGS) -shared $(BIN_DIR)/isp.o -o $(BIN_DIR)/libisp.so $(LDFLAGS)

jni: $(BIN_DIR)/libisp_jni.so $(BIN_DIR)/CameraHalBridge.class

$(BIN_DIR)/libisp_jni.so: src/jni_bridge.c $(BIN_DIR)/isp.o
	$(CC) $(CFLAGS) $(JNI_INC) -fPIC -shared src/jni_bridge.c $(BIN_DIR)/isp.o -o $(BIN_DIR)/libisp_jni.so $(LDFLAGS)

$(BIN_DIR)/CameraHalBridge.class: java/CameraHalBridge.java
	mkdir -p $(BIN_DIR)
	javac -d $(BIN_DIR) java/CameraHalBridge.java

run: $(BIN_DIR)/isp
	./$(BIN_DIR)/isp

run-v4l2: $(BIN_DIR)/v4l2
	./$(BIN_DIR)/v4l2 --simulate

run-hal3: $(BIN_DIR)/hal3
	./$(BIN_DIR)/hal3

run-jni: jni
	java --enable-native-access=ALL-UNNAMED -Djava.library.path=$(BIN_DIR) -cp $(BIN_DIR) CameraHalBridge

run-py: lib
	python3 scripts/tune_isp.py

run-all: run run-v4l2 run-hal3 run-jni run-py

clean:
	rm -rf $(BIN_DIR) output
