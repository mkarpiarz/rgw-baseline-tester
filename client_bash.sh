#!/bin/bash

if [ $# -lt 2 ]
then
    cat <<- EOF
    Usage: $0 host port
EOF
    exit 1
fi

host="$1"
port="$2"
curl -v -H "Expect: 100-continue" --data '*' http://$host:$port
