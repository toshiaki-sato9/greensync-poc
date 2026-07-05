#!/bin/bash

cd firmware/atom-s3-lite
pio run -t upload && pio device monitor
