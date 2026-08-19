#!/bin/bash
./start_ecat.sh | while read line; do
    echo "$line"
    if echo "$line" | grep -q "JITTER WARNING"; then
        echo "$(date) - $line" >> jitter_warnings.log
        echo -e "\033[31m[!] JITTER SPIKE DETECTED!\033[0m"
    fi
done