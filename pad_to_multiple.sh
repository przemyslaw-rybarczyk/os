#!/bin/bash

# Usage:
# ./pad_to_multiple.sh FILE BLOCK_SIZE
# Pads file FILE with zeroes until its size is a multiple of BLOCK_SIZE

old_size=$(wc -c $1 | awk '{print $1}')
padded_size=$(( (($old_size - 1) / $2 + 1) * $2 ))
truncate $1 -s $padded_size
