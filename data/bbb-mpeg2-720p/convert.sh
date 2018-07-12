#!/bin/sh

export DISPLAY=:0.0
export LIBVA_DRIVER_NAME=dump
export DUMP_COUNT=25
cvlc "$1" > frames.h
