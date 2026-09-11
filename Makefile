CXX = g++
CXXFLAGS = -std=c++20 -O2 -Wall -Iinclude -Ithird_party -Ithird_party/minhook/include
LDFLAGS = -static -lws2_32 -lshlwapi -lcrypt32 -lwinhttp

BIN_DIR = bin
BUILD_DIR = build

DLL_TARGET = $(BIN_DIR)/lepus_core.dll
LAUNCHER_TARGET = $(BIN_DIR)/lepus_launcher.exe

all: $(DLL_TARGET) $(LAUNCHER_TARGET)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(DLL_TARGET): src/dllmain.cpp third_party/minhook/libminhook.a | $(BIN_DIR)
	$(CXX) -shared $(CXXFLAGS) src/dllmain.cpp third_party/minhook/libminhook.a $(LDFLAGS) -o $(DLL_TARGET)

$(LAUNCHER_TARGET): src/launcher_main.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) src/launcher_main.cpp $(LDFLAGS) -o $(LAUNCHER_TARGET)

clean:
	rm -rf $(BIN_DIR)

.PHONY: all clean
