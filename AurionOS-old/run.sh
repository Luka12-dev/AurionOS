#!/bin/bash

# AurionOS Build & Run Script

# Clean previous build artifacts (Optional)
# make clean 

# Build the system
echo "Building AurionOS..."
make all

# Run in QEMU (defaulting to HDD image)
echo "Starting AurionOS in QEMU..."
make run

# Alternative: Run as ISO
# make run-iso 

# Information
# make clean    - Clean build artifacts
# make all      - Compile assembly and C source files
# make run      - Launch QEMU with disk image

echo "---------------------------------------------------"
echo "WARNING: For building AurionOS, Linux (Ubuntu) is recommended."
echo "Windows users should use WSL (Ubuntu 24.04 recommended)."
echo "---------------------------------------------------"

echo "AurionOS has shut down."