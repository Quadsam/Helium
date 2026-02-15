#!/usr/bin/env python3

import os
import subprocess
import glob
import sys

# Configuration
COMPILER = "bin/heliumc"  # Path to your compiler binary
TEST_DIR = "tests"        # Directory where .he files live
TMP_ASM = "test_tmp.s"
TMP_OBJ = "test_tmp.o"
TMP_EXE = "test_tmp"

RED = "\033[91m"
GREEN = "\033[92m"
RESET = "\033[0m"

def parse_expectations(filepath):
	expected_exit = 0 # Default to 0 if not specified
	expected_out = ""
	has_expect_exit = False
	
	with open(filepath, "r") as f:
		for line in f:
			if "// expect-exit:" in line:
				expected_exit = int(line.split(":")[-1].strip())
				has_expect_exit = True
			if "// expect-out:" in line:
				# Capture everything after the colon
				expected_out += line.split(":", 1)[-1].lstrip()
				
	return expected_exit, expected_out.strip()

def run_test(filepath):
	print(f"Testing {filepath}...", end=" ")
	sys.stdout.flush()
	
	# 1. Compile
	# We use capture_output=True so we don't spam the console unless it fails
	compile_cmd = [COMPILER, "-o", TMP_ASM, filepath]
	comp_res = subprocess.run(compile_cmd, capture_output=True)
	
	if comp_res.returncode != 0:
		print(f"{RED}FAIL (Compilation Error){RESET}")
		print(comp_res.stderr.decode())
		return False

	# 2. Assemble (NASM)
	nasm_res = subprocess.run(["nasm", "-f", "elf64", TMP_ASM, "-o", TMP_OBJ], capture_output=True)
	if nasm_res.returncode != 0:
		print(f"{RED}FAIL (Assembler Error){RESET}")
		print(nasm_res.stderr.decode())
		return False

	# 3. Link (LD)
	ld_res = subprocess.run(["ld", TMP_OBJ, "-o", TMP_EXE], capture_output=True)
	if ld_res.returncode != 0:
		print(f"{RED}FAIL (Linker Error){RESET}")
		print(ld_res.stderr.decode())
		return False

	# 4. Run the executable
	expected_exit, expected_out = parse_expectations(filepath)
	
	try:
		run_res = subprocess.run([f"./{TMP_EXE}"], capture_output=True)
		actual_exit = run_res.returncode
		actual_out = run_res.stdout.decode().strip()
	except Exception as e:
		print(f"{RED}FAIL (Runtime Error){RESET}")
		print(e)
		return False

	# 5. Verify
	if actual_exit != expected_exit:
		print(f"{RED}FAIL (Wrong Exit Code){RESET}")
		print(f"  Expected: {expected_exit}")
		print(f"  Actual:   {actual_exit}")
		return False
		
	if expected_out and actual_out != expected_out:
		print(f"{RED}FAIL (Wrong Output){RESET}")
		print(f"  Expected: '{expected_out}'")
		print(f"  Actual:   '{actual_out}'")
		return False

	print(f"{GREEN}PASS{RESET}")
	return True

def clean_up():
	for f in [TMP_ASM, TMP_OBJ, TMP_EXE]:
		if os.path.exists(f):
			os.remove(f)

def main():
	if not os.path.exists(COMPILER):
		print(f"Compiler not found at '{COMPILER}'! Run 'make' first.")
		return

	# Create tests dir if it doesn't exist
	if not os.path.exists(TEST_DIR):
		os.makedirs(TEST_DIR)
		print(f"Created '{TEST_DIR}' directory. Add .he files there!")
		return

	tests = glob.glob(f"{TEST_DIR}/*.he")
	if not tests:
		print(f"No .he files found in {TEST_DIR}/")
		return

	tests.sort() # Run in alphabetical order
	passed = 0
	total = 0

	for test in tests:
		total += 1
		if run_test(test):
			passed += 1

	clean_up()
	
	print("-" * 30)
	print(f"Result: {passed}/{total} passed")
	
	if passed != total:
		sys.exit(1)

if __name__ == "__main__":
	main()
