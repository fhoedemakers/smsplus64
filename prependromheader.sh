#!/bin/bash

# Check if the required argument is provided
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <binary_file>"
    exit 1
fi

# Assign input argument to variable
binary_file="$1"

# Extract the filename, extension, and construct the new output filename
filename="${binary_file%.*}"
extension="${binary_file##*.}"

if [ "$filename" == "$binary_file" ]; then
    # If no extension, just append _512hdr
    output_file="${binary_file}_512hdr"
else
    # Append _512hdr before the extension
    output_file="${filename}_512hdr.${extension}"
fi

# Create a 512-byte zero header and prepend it to the binary file
dd if=/dev/zero bs=512 count=1 2>/dev/null | cat - "$binary_file" > "$output_file"

# Check if the output file was created successfully
if [ $? -eq 0 ]; then
    echo "Successfully prepended 512 zero bytes to the binary file."
    echo "Output written to: $output_file"
else
    echo "Error: Failed to prepend the header."
    exit 1
fi

