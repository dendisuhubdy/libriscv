#!/bin/bash
curl --data-binary "@test.txt" -X POST http://localhost:1234/exec
