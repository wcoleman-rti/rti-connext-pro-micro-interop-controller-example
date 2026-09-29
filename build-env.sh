#!/bin/bash

# =========================================
# Build environment

# Connext Pro
export NDDSHOME=/opt/rti.com/rti_connext_dds-7.7.0
export CONNEXTDDS_ARCH=x64Linux4gcc8.5.0

# Connext Micro
export RTIMEHOME=/opt/rti.com/rti_connext_dds_micro-4.3.0
export RTIME_TARGET_NAME=x86_64leElfgcc13.3.0CERT-Linux6

export JREHOME=$NDDSHOME/resource/app/jre/x64Linux
