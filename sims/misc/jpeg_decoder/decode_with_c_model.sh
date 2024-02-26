#!/bin/bash

# Check if the input and output directories are provided
if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Usage: $0 <input_directory> <output_directory>"
    exit 1
fi

# Set the input and output directories
input_directory="$1"
output_directory="$2"

# Check if the input directory exists
if [ ! -d "$input_directory" ]; then
    echo "Error: Input directory '$input_directory' not found."
    exit 1
fi

# Create the output directory if it doesn't exist
mkdir -p "$output_directory"

# Loop through all JPEG files in the directory
for jpeg_file in "$input_directory"/*.jpg; do
    if [ -f "$jpeg_file" ]; then
        ppm_file="$output_directory/$(basename "${jpeg_file%.jpg}.ppm")"
        echo "Converting $jpeg_file to $ppm_file"
        ./rtl/jpeg_core/c_model/jpeg "$jpeg_file" "$ppm_file"
    fi
done
