CXX = g++

# Compiler Flags:
# -I.  -> Look for header files (like stb_image.h) in the current directory
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I.

# Linker Flags:
# -lGLEW -> Required for Modern OpenGL extension handling
# -lGL   -> Core OpenGL
# -lSDL2 -> Window management
# -lm    -> Math library
LDFLAGS = -lSDL2 -lGL -lGLEW -lm

TARGET = rubiks_cube
SRC = main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
