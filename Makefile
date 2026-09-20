CXXFLAGS ?= -O2
PLUGIN_FLAGS = -shared -fPIC -std=c++2b -Wno-narrowing
PKGS = pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon
ifeq ($(shell $(CXX) --version 2>&1 | grep -c g++),1)
EXTRA_FLAGS = -fno-gnu-unique
endif

all:
	$(CXX) $(CXXFLAGS) $(PLUGIN_FLAGS) $(EXTRA_FLAGS) \
	  $(wildcard src/cube/*.cpp src/gl/*.cpp src/hypr/*.cpp) \
	  -o hypr-cube.so `pkg-config --cflags $(PKGS)`

test:
	@mkdir -p build
	$(CXX) -std=c++2b -g -O0 -Wall -Wextra src/cube/*.cpp test/*.cpp -o build/test_cube
	./build/test_cube

.PHONY: all test
