#!/bin/bash

# Directory containing C test files
TEST_DIR="tests"
LOG_FILE="test_results.log"
OUTPUT_DIR="output" # Directory to store compiled binaries and runtime outputs
NUM_RUNS=20

# Create or clear the log file
: >"$LOG_FILE"

# Ensure the output directory exists
mkdir -p "$OUTPUT_DIR"

# Check if the test directory exists
if [ ! -d "$TEST_DIR" ]; then
    echo "Error: Test directory '$TEST_DIR' not found."
    exit 1
fi

# Function to calculate average time
calculate_average() {
    local total=0
    local count=0
    for time in "$@"; do
        total=$(echo "$total + $time" | bc)
        count=$((count + 1))
    done
    echo "scale=3; $total / $count" | bc
}

compile() {
    TEST_FILE=$1
    DIR_NAME=$(dirname "$TEST_FILE")
    BASE_NAME=$(basename "$TEST_FILE" .c)
    RELATIVE_PATH="$DIR_NAME/$BASE_NAME"
    CFLAGS="-O0 -S -emit-llvm -Xclang -disable-O0-optnone"

    clang $CFLAGS "$TEST_FILE" -o "${RELATIVE_PATH}.ll"
    clang -O1 -S -emit-llvm "$TEST_FILE" -o "${RELATIVE_PATH}_o1.ll"

    # Measure pass time
    COMPILE_START=$(date +%s%N)
    # Apply passes
    opt -load-pass-plugin "build/src/PeepHole/PeepHolePass.so" \
        -load-pass-plugin "build/src/sroa/SROAPass.so" \
        -passes="mysroa,peephole" "${RELATIVE_PATH}.ll" -S -o "${RELATIVE_PATH}_with_pass.ll" -debug-pass-manager
    COMPILE_END=$(date +%s%N)
    COMPILE_TIME=$((($COMPILE_END - $COMPILE_START) / 1000000))
    llvm-as "${RELATIVE_PATH}.ll" -o "${RELATIVE_PATH}.bc"
    llvm-as "${RELATIVE_PATH}_with_pass.ll" -o "${RELATIVE_PATH}_with_pass.bc"
    llvm-as "${RELATIVE_PATH}_o1.ll" -o "${RELATIVE_PATH}_o1.bc"
    clang "${RELATIVE_PATH}.bc" -o "${RELATIVE_PATH}"
    clang "${RELATIVE_PATH}_with_pass.bc" -o "${RELATIVE_PATH}_with_pass"
    clang "${RELATIVE_PATH}_o1.bc" -o "${RELATIVE_PATH}_o1"

    echo "Compile Finished" | tee -a "$LOG_FILE"
}

cleanup() {
    TEST_FILE=$1
    DIR_NAME=$(dirname "$TEST_FILE")
    BASE_NAME=$(basename "$TEST_FILE" .c)
    RELATIVE_PATH="$DIR_NAME/$BASE_NAME"
    rm -f "${RELATIVE_PATH}" "${RELATIVE_PATH}_with_pass" "${RELATIVE_PATH}_o1"
    rm -f "${RELATIVE_PATH}.ll" "${RELATIVE_PATH}_with_pass.ll" "${RELATIVE_PATH}_o1.ll"
    rm -f "${RELATIVE_PATH}.bc" "${RELATIVE_PATH}_with_pass.bc" "${RELATIVE_PATH}_o1.bc"
}

# Iterate over each C test file in the directory
for TEST_FILE in "$TEST_DIR"/*.c; do
    if [ -f "$TEST_FILE" ]; then
        echo "Running test: $TEST_FILE" | tee -a "$LOG_FILE"

        BASENAME=$(basename "$TEST_FILE" .c)
        DIR_NAME=$(dirname "$TEST_FILE")
        RELATIVE_PATH="$DIR_NAME/$BASENAME"
        BIN_WITHOUT_PASS="$RELATIVE_PATH"
        BIN_WITH_PASS="${RELATIVE_PATH}_with_pass"
        BIN_WITH_O1="${RELATIVE_PATH}_o1"
        RUN_OUTPUT_WITHOUT_PASS="${OUTPUT_DIR}/${BASENAME}_output_without_pass.txt"
        RUN_OUTPUT_WITH_PASS="${OUTPUT_DIR}/${BASENAME}_output_with_pass.txt"
        RUN_OUTPUT_WITH_O1="${OUTPUT_DIR}/${BASENAME}_output_with_O1.txt"

        # Compile the test file to llvm IR
        compile "$TEST_FILE"

        CODE_SIZE=$(llvm-size "${RELATIVE_PATH}" | tail -n 1 | awk '{print $1}')
        CODE_SIZE_WITH_PASS=$(llvm-size "${RELATIVE_PATH}_with_pass" | tail -n 1 | awk '{print $1}')
        CODE_SIZE_O1=$(llvm-size "${RELATIVE_PATH}_o1" | tail -n 1 | awk '{print $1}')

        # Measure runtime without the LLVM pass
        times_without_pass=()
        mem_without_pass=()
        echo "Running without pass..." | tee -a "$LOG_FILE"
        for ((i = 0; i < NUM_RUNS; i++)); do
            output=$({ /usr/bin/time -f "%e %M" "$BIN_WITHOUT_PASS" >"$RUN_OUTPUT_WITHOUT_PASS"; } 2>&1)
            TIME_WITHOUT_PASS=$(echo "$output" | awk '{print $1}')
            MEM_WITHOUT_PASS=$(echo "$output" | awk '{print $2}')
            times_without_pass+=("$TIME_WITHOUT_PASS")
            mem_without_pass+=("$MEM_WITHOUT_PASS")
        done
        AVG_TIME_WITHOUT_PASS=$(calculate_average "${times_without_pass[@]}")
        AVG_MEM_WITHOUT_PASS=$(calculate_average "${mem_without_pass[@]}")

        # Measure runtime with the LLVM pass
        echo "Running with pass..." | tee -a "$LOG_FILE"
        times_with_pass=()
        mem_with_pass=()
        for ((i = 0; i < NUM_RUNS; i++)); do
            output=$({ /usr/bin/time -f "%e %M" "$BIN_WITH_PASS" >"$RUN_OUTPUT_WITH_PASS"; } 2>&1)
            TIME_WITH_PASS=$(echo "$output" | awk '{print $1}')
            MEM_WITH_PASS=$(echo "$output" | awk '{print $2}')
            times_with_pass+=("$TIME_WITH_PASS")
            mem_with_pass+=("$MEM_WITH_PASS")
        done
        AVG_TIME_WITH_PASS=$(calculate_average "${times_with_pass[@]}")
        AVG_MEM_WITH_PASS=$(calculate_average "${mem_with_pass[@]}")

        # Measure runtime with the O1
        echo "Running with O1..." | tee -a "$LOG_FILE"
        times_with_o1=()
        mem_with_o1=()
        for ((i = 0; i < NUM_RUNS; i++)); do
            output=$({ /usr/bin/time -f "%e %M" "$BIN_WITH_O1" >"$RUN_OUTPUT_WITH_O1"; } 2>&1)
            TIME_WITH_O1=$(echo "$output" | awk '{print $1}')
            MEM_WITH_O1=$(echo "$output" | awk '{print $2}')
            times_with_o1+=("$TIME_WITH_O1")
            mem_with_o1+=("$MEM_WITH_O1")
        done
        AVG_TIME_WITH_O1=$(calculate_average "${times_with_o1[@]}")
        AVG_MEM_WITH_O1=$(calculate_average "${mem_with_o1[@]}")

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
        echo "Average runtime without pass: $AVG_TIME_WITHOUT_PASS s" | tee -a "$LOG_FILE"
        echo "Average memory usage without pass: $AVG_MEM_WITHOUT_PASS KB" | tee -a "$LOG_FILE"
        echo "Average runtime with pass:    $AVG_TIME_WITH_PASS s" | tee -a "$LOG_FILE"
        echo "Average memory usage with pass: $AVG_MEM_WITH_PASS KB" | tee -a "$LOG_FILE"
        echo "Average runtime with -O1:     $AVG_TIME_WITH_O1 s" | tee -a "$LOG_FILE"
        echo "Average memory usage with -O1: $AVG_MEM_WITH_O1 KB" | tee -a "$LOG_FILE"
        echo "IR Code size without pass:       $CODE_SIZE bytes" | tee -a "$LOG_FILE"
        echo "IR Code size with pass:          $CODE_SIZE_WITH_PASS bytes" | tee -a "$LOG_FILE"
        echo "IR Code size with -O1:           $CODE_SIZE_O1 bytes" | tee -a "$LOG_FILE"
        echo "Pass running time:             $COMPILE_TIME ms" | tee -a "$LOG_FILE"
        echo "Output check:                 $OUTPUT_CHECK" | tee -a "$LOG_FILE"
        echo "---------------------------------" | tee -a "$LOG_FILE"

        # Clean up the generated files
        cleanup "$TEST_FILE"
    else
        echo "No C files found in '$TEST_DIR'."
    fi
done

echo "Testing complete. Results saved in $LOG_FILE."
#
# #!/bin/bash
#
# # Directory containing C test files
# TEST_DIR="tests"
# LOG_FILE="test_results.log"
# OUTPUT_DIR="output" # Directory to store compiled binaries and runtime outputs
# NUM_RUNS=1
# # Create or clear the log file
# : >"$LOG_FILE"
#
# # Ensure the output directory exists
# mkdir -p "$OUTPUT_DIR"
#
# # Check if the test directory exists
# if [ ! -d "$TEST_DIR" ]; then
#     echo "Error: Test directory '$TEST_DIR' not found."
#     exit 1
# fi
#
# # Function to calculate average time
# calculate_average() {
#     local total=0
#     local count=0
#     for time in "$@"; do
#         total=$(echo "$total + $time" | bc)
#         count=$((count + 1))
#     done
#     echo "scale=3; $total / $count" | bc
# }
#
# compile() {
#     TEST_FILE=$1
#     DIR_NAME=$(dirname "$TEST_FILE")
#     BASE_NAME=$(basename "$TEST_FILE" .c)
#     RELATIVE_PATH="$DIR_NAME/$BASE_NAME"
#     CFLAGS="-O0 -S -emit-llvm -Xclang -disable-O0-optnone"
#     clang $CFLAGS "$TEST_FILE" -o "${RELATIVE_PATH}.ll"
#     # Both apply sroa pass
#     opt -passes="" "${RELATIVE_PATH}.ll" -S -o "${RELATIVE_PATH}.ll"
#     # apply peephole pass
#     echo "Outputs of the pass:" | tee -a "$LOG_FILE"
#     time -p opt -load-pass-plugin "build/src/PeepHole/PeepHolePass.so" \
#         -load-pass-plugin "build/src/sroa/SROAPass.so" \
#         -passes="function(mysroa,peephole)" "${RELATIVE_PATH}.ll" -S -o "${RELATIVE_PATH}_with_pass.ll" -debug-pass-manager
#     # Compile them to bytecode
#     llvm-as "${RELATIVE_PATH}.ll" -o "${RELATIVE_PATH}.bc"
#     llvm-as "${RELATIVE_PATH}_with_pass.ll" -o "${RELATIVE_PATH}_with_pass.bc"
#     # compile them to executables
#     clang "${RELATIVE_PATH}.bc" -o "${RELATIVE_PATH}"
#     clang "${RELATIVE_PATH}_with_pass.bc" -o "${RELATIVE_PATH}_with_pass"
#     clang -O1 "$TEST_FILE" -o "${RELATIVE_PATH}_o1"
#     echo "Compile Finished" | tee -a "$LOG_FILE"
# }
#
# cleanup() {
#     TEST_FILE=$1
#     DIR_NAME=$(dirname "$TEST_FILE")
#     BASE_NAME=$(basename "$TEST_FILE" .c)
#     RELATIVE_PATH="$DIR_NAME/$BASE_NAME"
#     rm -f "${RELATIVE_PATH}" "${RELATIVE_PATH}_with_pass" "${RELATIVE_PATH}_o1"
#     rm -f "${RELATIVE_PATH}.ll" "${RELATIVE_PATH}_with_pass.ll"
#     rm -f "${RELATIVE_PATH}.bc" "${RELATIVE_PATH}_with_pass.bc"
# }
#
# # Iterate over each C test file in the directory
# for TEST_FILE in "$TEST_DIR"/*.c; do
#     if [ -f "$TEST_FILE" ]; then
#         echo "Running test: $TEST_FILE" | tee -a "$LOG_FILE"
#
#         BASENAME=$(basename "$TEST_FILE" .c)
#         DIR_NAME=$(dirname "$TEST_FILE")
#         RELATIVE_PATH="$DIR_NAME/$BASENAME"
#         BIN_WITHOUT_PASS="$RELATIVE_PATH"
#         BIN_WITH_PASS="${RELATIVE_PATH}_with_pass"
#         BIN_WITH_O1="${RELATIVE_PATH}_o1"
#         RUN_OUTPUT_WITHOUT_PASS="${OUTPUT_DIR}/${BASENAME}_output_without_pass.txt"
#         RUN_OUTPUT_WITH_PASS="${OUTPUT_DIR}/${BASENAME}_output_with_pass.txt"
#         RUN_OUTPUT_WITH_O1="${OUTPUT_DIR}/${BASENAME}_output_with_O1.txt"
#
#         # Compile the test file to llvm IR
#         compile "$TEST_FILE"
#         # Measure runtime without the LLVM pass
#         echo "Running without pass..." | tee -a "$LOG_FILE"
#         times_without_pass=()
#
#         for ((i = 0; i < NUM_RUNS; i++)); do
#             TIME_WITHOUT_PASS=$({ time "$BIN_WITHOUT_PASS" >"$RUN_OUTPUT_WITHOUT_PASS"; } 2>&1 | grep real | awk '{print $2}' | sed 's/[^0-9.]//g')
#             times_without_pass+=("$TIME_WITHOUT_PASS")
#         done
#         AVG_TIME_WITHOUT_PASS=$(calculate_average "${times_without_pass[@]}")
#
#         # Measure runtime with the LLVM pass
#         echo "Running with pass..." | tee -a "$LOG_FILE"
#         times_with_pass=()
#         for ((i = 0; i < NUM_RUNS; i++)); do
#             TIME_WITH_PASS=$({ time "$BIN_WITH_PASS" >"$RUN_OUTPUT_WITH_PASS"; } 2>&1 | grep real | awk '{print $2}' | sed 's/[^0-9.]//g')
#             times_with_pass+=("$TIME_WITH_PASS")
#         done
#         AVG_TIME_WITH_PASS=$(calculate_average "${times_with_pass[@]}")
#
#         # Measure runtime with the O1
#         echo "Running with O1..." | tee -a "$LOG_FILE"
#         times_with_o1=()
#         for ((i = 0; i < NUM_RUNS; i++)); do
#             TIME_WITH_PASS=$({ time "$BIN_WITH_O1" >"$RUN_OUTPUT_WITH_O1"; } 2>&1 | grep real | awk '{print $2}' | sed 's/[^0-9.]//g')
#             times_with_o1+=("$TIME_WITH_PASS")
#         done
#         AVG_TIME_WITH_O1=$(calculate_average "${times_with_o1[@]}")
#
#         # Compare runtime outputs
#         if cmp -s "$RUN_OUTPUT_WITHOUT_PASS" "$RUN_OUTPUT_WITH_PASS"; then
#             OUTPUT_CHECK="Outputs match"
#             RESULT="PASSED"
#         else
#             OUTPUT_CHECK="Outputs differ"
#             RESULT="FAILED"
#         fi
#
#         # Log the results
#         echo "Test $TEST_FILE: $RESULT" | tee -a "$LOG_FILE"
#         echo "Average runtime without pass: $AVG_TIME_WITHOUT_PASS" | tee -a "$LOG_FILE"
#         echo "Average runtime with pass:    $AVG_TIME_WITH_PASS" | tee -a "$LOG_FILE"
#         echo "Average runtime with -O1:     $AVG_TIME_WITH_O1" | tee -a "$LOG_FILE"
#         echo "Output check:                 $OUTPUT_CHECK" | tee -a "$LOG_FILE"
#         echo "---------------------------------" | tee -a "$LOG_FILE"
#
#         # Clean up the generated files
#         cleanup "$TEST_FILE"
#     else
#         echo "No C files found in '$TEST_DIR'."
#     fi
# done
#
# echo "Testing complete. Results saved in $LOG_FILE."
