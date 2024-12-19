#!/bin/bash

# Directory containing C test files
TEST_DIR="tests"
PLUGIN_PATH=$(echo build/src/PeepHolePass.*) # Adjust the path if necessary
LOG_FILE="test_results.log"
OUTPUT_DIR="output" # Directory to store compiled binaries and runtime outputs

# Create or clear the log file
: >"$LOG_FILE"

# Ensure the output directory exists
mkdir -p "$OUTPUT_DIR"

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

        # Define output file names
        BASE_NAME=$(basename "$TEST_FILE" .c)
        BIN_WITHOUT_PASS="$OUTPUT_DIR/${BASE_NAME}_nopass"
        BIN_WITH_PASS="$OUTPUT_DIR/${BASE_NAME}_pass"
        RUN_OUTPUT_WITHOUT_PASS="$OUTPUT_DIR/${BASE_NAME}_nopass_output.txt"
        RUN_OUTPUT_WITH_PASS="$OUTPUT_DIR/${BASE_NAME}_pass_output.txt"

        # Compile without the LLVM pass
        echo "Compiling without pass..." | tee -a "$LOG_FILE"
        clang "$TEST_FILE" -o "$BIN_WITHOUT_PASS"

        # Compile with the LLVM pass
        echo "Compiling with pass..." | tee -a "$LOG_FILE"
        clang -fpass-plugin="$PLUGIN_PATH" "$TEST_FILE" -o "$BIN_WITH_PASS"

        # Measure runtime without the LLVM pass
        echo "Running without pass..." | tee -a "$LOG_FILE"
        TIME_WITHOUT_PASS=$({ time "$BIN_WITHOUT_PASS" >"$RUN_OUTPUT_WITHOUT_PASS"; } 2>&1 | grep real | awk '{print $2}')

        # Measure runtime with the LLVM pass
        echo "Running with pass..." | tee -a "$LOG_FILE"
        TIME_WITH_PASS=$({ time "$BIN_WITH_PASS" >"$RUN_OUTPUT_WITH_PASS"; } 2>&1 | grep real | awk '{print $2}')

        # Compare runtime outputs
        if cmp -s "$RUN_OUTPUT_WITHOUT_PASS" "$RUN_OUTPUT_WITH_PASS"; then
            OUTPUT_CHECK="Outputs match"
            RESULT="PASSED"
        else
            OUTPUT_CHECK="Outputs differ"
            RESULT="FAILED"
        fi

        # Log the results
        echo "Test $TEST_FILE: $RESULT" | tee -a "$LOG_FILE"
        echo "Runtime without pass: $TIME_WITHOUT_PASS" | tee -a "$LOG_FILE"
        echo "Runtime with pass:    $TIME_WITH_PASS" | tee -a "$LOG_FILE"
        echo "Output check:         $OUTPUT_CHECK" | tee -a "$LOG_FILE"
        echo "---------------------------------" | tee -a "$LOG_FILE"
    else
        echo "No C files found in '$TEST_DIR'."
    fi
done

# Summary
echo "Testing complete. Results saved in $LOG_FILE."
