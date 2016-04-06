#!/bin/bash

# Clean Dia's temporary files
find * -type f -name "*dia~" -exec rm -f {} \;

# Create png images from Dia files
find * -type f -name "*.dia" -exec dia -t png {} \;
