#!/usr/bin/env python3
"""
Generate a random file of a specified size. The file is named based on the size
string the user passes.

Usage examples:
    python generate_random_file.py 10000
    python generate_random_file.py 10MB
    python generate_random_file.py 2GB
"""

import sys
import os
import re

def parse_size(size_str):
    """
    Parse a size string (e.g. "10000", "10MB", "2GB") into an integer (number of bytes).
    Supported units (case-insensitive): K, KB, M, MB, G, GB
    If no unit is given, assume bytes.
    """
    # Regex to extract numeric part and optional unit
    match = re.match(r"^\s*(\d+(?:\.\d+)?)\s*([KMG]?B?)?\s*$", size_str, re.IGNORECASE)
    if not match:
        raise ValueError(f"Invalid size format: {size_str}")

    number_part = float(match.group(1))
    unit_part = match.group(2).upper() if match.group(2) else ""

    # Mapping units to a multiplier
    multipliers = {
        "": 1,       # no suffix means bytes
        "B": 1,      # explicitly B
        "K": 1024,   # "K" implies KB in many contexts
        "KB": 1024,
        "M": 1024**2,
        "MB": 1024**2,
        "G": 1024**3,
        "GB": 1024**3
    }

    # Remove trailing "B" if the unit is like "MB", "GB"
    # (The dictionary accounts for it, but let's be sure)
    if unit_part not in multipliers:
        raise ValueError(f"Unknown unit: {unit_part}")

    size_in_bytes = int(number_part * multipliers[unit_part])
    return size_in_bytes

def generate_random_file(size_str):
    """
    Generates a random file named after size_str, containing size_in_bytes of random data.
    """
    file_size = parse_size(size_str)
    file_name = size_str  # The user wants the file named with the exact passed string

    # Create the file with random data
    chunk_size = 1024 * 1024  # 1 MB chunk
    bytes_written = 0

    with open(file_name, 'wb') as f:
        while bytes_written < file_size:
            # Write in chunks
            # Next chunk should not exceed the remaining bytes
            write_size = min(chunk_size, file_size - bytes_written)
            f.write(os.urandom(write_size))
            bytes_written += write_size

    print(f"Generated file '{file_name}' with size {file_size} bytes.")

def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <size>")
        print("Example: ")
        print(f"  {sys.argv[0]} 10000      (bytes)")
        print(f"  {sys.argv[0]} 10MB       (megabytes)")
        print(f"  {sys.argv[0]} 2GB        (gigabytes)")
        sys.exit(1)

    size_str = sys.argv[1]
    generate_random_file(size_str)

if __name__ == "__main__":
    main()
