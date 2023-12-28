#!/bin/bash
sudo cxl create-region -m -t ram -d decoder0.0 -w 1 -g 4096 mem0
sudo daxctl online-memory dax0.0

sudo daxctl offline-memory dax0.0
sudo daxctl reconfigure-device -m devdax dax0.0