# XexTool-RE - build the CLI and the GUI with MinGW g++ (C++17)
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -MMD -MP

# Engine = every source except the CLI entry points and the GUI entry point.
ENGINE_SRC := $(filter-out src/main.cpp src/selftest.cpp,$(wildcard src/*.cpp)) \
              $(wildcard src/crypto/*.cpp) $(wildcard src/compress/*.cpp)
ENGINE_OBJ := $(ENGINE_SRC:.cpp=.o)

CLI_OBJ := src/main.o src/selftest.o $(ENGINE_OBJ)
GUI_OBJ := src/gui/gui.o $(ENGINE_OBJ)
DEP     := $(CLI_OBJ:.o=.d) src/gui/gui.d

CLI := xextool-re.exe
GUI := xextool-re-gui.exe

all: $(CLI) $(GUI)
cli: $(CLI)
gui: $(GUI)

$(CLI): $(CLI_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(GUI): $(GUI_OBJ)
	$(CXX) $(CXXFLAGS) -mwindows -o $@ $^ -lcomdlg32 -lshell32 -lole32

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(ENGINE_OBJ) src/main.o src/selftest.o src/gui/gui.o $(DEP) $(CLI) $(GUI)

-include $(DEP)
.PHONY: all cli gui clean