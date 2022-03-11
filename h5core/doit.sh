#! /bin/bash

OPT=-O3
HERMES_ROOT=${DEV}/hermes
HERMES_INCLUDE="-I${HERMES_ROOT}/src -I${HERMES_ROOT}/src/api -I${HERMES_ROOT}/adapter \
                 -I${HERMES_ROOT}/adapter/vfd"
HERMES_LIB="-L${HB}/bin -Wl,-rpath,${HB}/bin"

mpicc -g3 ${OPT} -DUSE_CORE=1 -I${HOME}/local/include -L${HOME}/local/lib -Wl,-rpath,${HOME}/local/lib \
      h5core.c -lhdf5 -lhdf5_hl -o h5core

mpicc -g3 ${OPT} -I${HOME}/local/include -L${HOME}/local/lib -Wl,-rpath,${HOME}/local/lib \
      h5core.c -lhdf5 -lhdf5_hl -o h5hermes

mpicc -g3 ${OPT} -DUSE_LOG=1 -I${HOME}/local/include -L${HOME}/local/lib -Wl,-rpath,${HOME}/local/lib \
      h5core.c -lhdf5 -lhdf5_hl -o h5log

mpicc -g3 ${OPT} -DUSE_SPLIT=1 -I${HOME}/local/include -L${HOME}/local/lib -Wl,-rpath,${HOME}/local/lib \
      ${HERMES_INCLUDE} ${HERMES_LIB} h5core.c -lhdf5 -lhdf5_hl -l hdf5_hermes_vfd -o h5split

# HDF5_DRIVER=hermes HDF5_PLUGIN_PATH=${HB}/bin HERMES_CONF=./hermes.conf HDF5_DRIVER_CONFIG="false 5529600" ./h5hermes

# h5pcc=${CC:-cc}
# $h5pcc h5core.c -o h5core
