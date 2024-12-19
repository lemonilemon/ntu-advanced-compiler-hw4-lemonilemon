#!/bin/bash

# Directory containing C test files
TEST_DIR="tests"
PLUGIN_PATH=$(echo build/src/PeepHolePass.*) # Adjust the path if needed
LOG_FILE="test_results.log"

# Create or clear the log file
: >"$LOG_FILE"

# Check if the test directory exists
if [ ! -d "$TEST_DIR" ]; then
    echo "Error: Test directory '$TEST_DIR' not found."
    exit 1
fi

# Check if the plugin exists
if [ ! -f "$PLUGIN_PATH" ]; then
    echo "Error: LLVM pass plugin not found at '$PLUGIN_PATH'."
    exit 1
fi

# Iterate over each C test file in the directory
for TEST_FILE in "$TEST_DIR"/*.c; do
    if [ -f "$TEST_FILE" ]; then
        echo "Running test: $TEST_FILE" | tee -a "$LOG_FILE"

        # Run the LLVM pass via clang
        clang -fpass-plugin="$PLUGIN_PATH" "$TEST_FILE" -o /dev/null 2>>"$LOG_FILE"

        if [ $? -eq 0 ]; then
            echo "Test $TEST_FILE: PASSED" | tee -a "$LOG_FILE"
        else
            echo "Test $TEST_FILE: FAILED" | tee -a "$LOG_FILE"
        fi
    else
        echo "No C files found in '$TEST_DIR'."
    fi
done

# Summary
echo "Testing complete. Results saved in $LOG_FILE."
