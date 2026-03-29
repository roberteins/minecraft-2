CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pedantic
LDFLAGS := -lglut -lGL -lGLU -lpng

all: minecraft-2

minecraft-2: main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o $@ $(LDFLAGS)

run: minecraft-2
	./minecraft-2

clean:
	rm -f minecraft-2
