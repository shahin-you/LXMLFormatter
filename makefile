# Makefile for XML Formatter
# Compiler: GCC 13.3.0
# Standard: C++17

# Compiler flags
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror
CXXFLAGS += -Wcast-align -Wcast-qual -Wshadow
CXXFLAGS += -Woverloaded-virtual -Wmissing-include-dirs
CXXFLAGS += -Wno-unused-parameter -Wno-unknown-pragmas 

# Build type (default: release)
BUILD_TYPE ?= release

LDFLAGS :=
ifeq ($(BUILD_TYPE),debug)
    CXXFLAGS += -g -O0 -DDEBUG
    BUILD_DIR := bin/debug
else ifeq ($(BUILD_TYPE),sanitized)
    CXXFLAGS += -g -O0 -DDEBUG -fsanitize=address,undefined
    LDFLAGS += -fsanitize=address,undefined
    BUILD_DIR := bin/sanitized
else
    CXXFLAGS += -O3 -DNDEBUG -march=native -flto
    LDFLAGS += -flto -s
    BUILD_DIR := bin/release
endif

# Directories
SRC_DIR := src
TEST_DIR := tests
OBJ_DIR := $(BUILD_DIR)/obj
TEST_OBJ_DIR := $(BUILD_DIR)/test_obj

# Create necessary directories
TARGET := $(BUILD_DIR)/xmlformatter
TEST_TARGET := $(BUILD_DIR)/test_xmlformatter

SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
HEADERS := $(wildcard $(SRC_DIR)/*.h)
OBJECTS := $(SOURCES:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

TEST_SOURCES := $(wildcard $(TEST_DIR)/*.cpp)
TEST_OBJECTS := $(TEST_SOURCES:$(TEST_DIR)/%.cpp=$(TEST_OBJ_DIR)/%.o)

# Dependencies
DEPS := $(OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d)

# Default target
.PHONY: all
all: $(TARGET)

# Main target
$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@
	@echo "Build complete: $@"

# Compile source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# Test target
.PHONY: test
test: $(TEST_TARGET)
	@echo "Running tests..."
	@$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJECTS) $(filter-out $(OBJ_DIR)/main.o, $(OBJECTS))
	@mkdir -p $(dir $@)
	$(CXX) $^ $(LDFLAGS) -o $@

# Compile test files
$(TEST_OBJ_DIR)/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -MMD -MP -c $< -o $@

# Clean build files
.PHONY: clean
clean:
	@rm -rf bin/
	@echo "Clean complete"

# Clean and rebuild
.PHONY: rebuild
rebuild: clean all

# Debug build (clean, no sanitizers)
.PHONY: debug
debug:
	@$(MAKE) BUILD_TYPE=debug

# Sanitized build (with AddressSanitizer)
.PHONY: sanitized
sanitized:
	@$(MAKE) BUILD_TYPE=sanitized

# Release build
.PHONY: release
release:
	@$(MAKE) BUILD_TYPE=release

# Run the formatter
.PHONY: run
run: $(TARGET)
	@$(TARGET) $(ARGS)

# Memory check with Valgrind (uses release build)
.PHONY: memory-check
memory-check:
	@$(MAKE) BUILD_TYPE=release
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 $(BUILD_DIR)/test_xmlformatter

# Static analysis (optional - requires cppcheck)
.PHONY: analyze
analyze:
	@cppcheck --enable=all --suppress=missingIncludeSystem \
		--error-exitcode=1 --inline-suppr \
		-I $(SRC_DIR) $(SRC_DIR) $(TEST_DIR)

# Help
.PHONY: help
help:
	@echo "XML Formatter Makefile"
	@echo "====================="
	@echo "Targets:"
	@echo "  all ------------ Build the XML formatter (default)"
	@echo "  test ----------- Build and run tests"
	@echo "  clean ---------- Remove all build files"
	@echo "  rebuild -------- Clean and rebuild"
	@echo "  debug ---------- Build clean debug version (no sanitizers)"
	@echo "  sanitized ------ Build with AddressSanitizer (for memory testing)"
	@echo "  release -------- Build with optimization"
	@echo "  run ------------ Run the XML formatter (use ARGS=... for arguments)"
	@echo "  memory-check --- Run tests with Valgrind (release build)"
	@echo "  analyze -------- Run static analysis with cppcheck (if available)"
	@echo "  help ----------- Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  BUILD_TYPE=[debug|sanitized|release] - Set build type (default: release)"
	@echo "  ARGS=... ----------------------- Arguments for 'make run'"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build release version"
	@echo "  make debug              # Build debug version (VSCode friendly)"
	@echo "  make sanitized          # Build with AddressSanitizer"
	@echo "  make test               # Run tests"
	@echo "  make memory-check       # Check for leaks with Valgrind"
	@echo "  make run ARGS='input.xml output.xml'"

# Include dependencies
-include $(DEPS)

# Print variables for debugging
.PHONY: print-%
print-%:
	@echo $* = $($*)