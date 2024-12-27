import random

# Configuration
NUM_VARIABLES = 50  # Number of variables to declare
NUM_OPERATIONS = 1000  # Number of operations to generate
VARIABLE_NAME_PREFIX = "var"
OPERATIONS = ["+", "-", "*", "/"]  # Supported arithmetic operations


# Function to generate a random variable name
def random_variable():
    return f"{VARIABLE_NAME_PREFIX}{random.randint(0, NUM_VARIABLES - 1)}"


# Function to generate a random operation
def random_operation():
    return random.choice(OPERATIONS)


# Generate C code
def generate_c_code():
    cpp_code = ["#include <stdio.h>", "", "int main() {"]
    var = {}
    # Declare variables
    for i in range(NUM_VARIABLES):
        val = random.randint(0, 100)
        cpp_code.append(f"    int {VARIABLE_NAME_PREFIX}{i} = {val};")
        var[f"{VARIABLE_NAME_PREFIX}{i}"] = val

    cpp_code.append("")  # Add a blank line

    # Generate random arithmetic operations
    for _ in range(NUM_OPERATIONS):
        var1 = random_variable()
        var2 = random_variable()
        operation = random_operation()

        # Avoid division by zero
        if operation == "/" and var[var2] == 0:
            operation = "+"
        var[var1] = eval(f"{var[var1]} {operation.replace('/', '//')} {var[var2]}")

        cpp_code.append(f"    {var1} = {var1} {operation} {var2};")

        if var[var1] > 10000 or var[var1] < 10000:
            var[var1] = random.randint(0, 100)
            cpp_code.append(f"    {var1} = {var[var1]};")

    # Output variables
    for i in range(NUM_VARIABLES):
        cpp_code.append(f'    printf("%d", {VARIABLE_NAME_PREFIX}{i});')

    cpp_code.extend(["", "    return 0;", "}"])

    return "\n".join(cpp_code)


# Save generated C++ code to a file
def save_to_file(filename, code):
    with open(filename, "w") as f:
        f.write(code)


if __name__ == "__main__":
    filename = "random.c"
    code = generate_c_code()
    save_to_file(filename, code)
    print(f"Generated C code saved to {filename}")
