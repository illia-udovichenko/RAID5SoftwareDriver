CXX      := g++
CXXFLAGS := -std=c++14 -Wall -Wextra -Iinclude

# Source files
TEST_SRC := src/CRaidVolume.cpp test/test.cpp

# Object files
TEST_OBJ := $(TEST_SRC:.cpp=.o)

# Executables
TEST_EXEC := raidTest

#-----------------------------------------
# Build test executable
$(TEST_EXEC): $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Compile .cpp to .o
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(TEST_OBJ) $(TEST_EXEC) tmp_*.bin

# Run tests
test: $(TEST_EXEC)
	@echo "Running tests..."
	./$(TEST_EXEC)

.PHONY: all clean test
