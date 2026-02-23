CXX     = g++
CXXFLAGS = -std=c++11 -Wall -I. -I../../lib/GFX -I../../lib/LM215
TARGET  = test_gfx

$(TARGET): test_gfx.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: run clean
