# HPC Safety Controller

## Build

```sh
. build-env.sh
cmake -S . -B build-cert -DRTIME_TARGET_NAME="$RTIME_TARGET_NAME" \
  -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cert --target safety_controller hpc_node
```

## Run

Start the Micro Safety Controller in one terminal:

```sh
. run-env.sh
./build-cert/src/micro/safety_controller enable
```

Start one Professional HPC process in each of two other terminals. The argument
is the numeric `HpcId` carried as an unsigned long in the keyed DDS samples:

```sh
. run-env.sh
./build-cert/src/pro/hpc_node 1
```

```sh
. run-env.sh
./build-cert/src/pro/hpc_node 2
```

The controller sends a command to each key and reports keyed health and
telemetry. Replace `enable` with `noop`, `disable`, or `reset` to select the
startup command. Stop processes with Ctrl-C.

## Configuration

`config/system/HpcSafetySystem.xml` is the complete MAG topology and contains
the controller plus both HPC peers in one participant library. The MAG model
spells out each HPC endpoint set so resource sizing sees both peers. The Pro
runtime keeps the repeated HPC participant structure in
`config/runtime/HpcSafetyProRuntime.xml` and derives `hpc_1` and `hpc_2` from
one participant template. Micro and Professional QoS are kept separate because
their transport descriptors and supported policies differ.