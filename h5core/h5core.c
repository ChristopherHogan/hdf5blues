
#include "hdf5.h"
#include "hdf5_hl.h"
#include "mpi.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define INCREMENT 4*1024*1024
#define MAX_DATASETS 10
#define MAX_GROUPS 10
#define MAX_ITER 5
#define MAX_LEN 255
#define MAX_TIME_LEVEL 70
#define PAGE_SIZE 512*1024*1024
#define RANK 3
#define X 48
#define Y 48
#define Z 600

int main(int argc, char** argv)
{
  int rank;
  hid_t fapl, file, group, group1, dset;
  size_t incr = INCREMENT;
  unsigned iter, level, igroup, idset;
  char name[MAX_LEN];
  hsize_t dims[3] = { X, Y, Z };
  float* buf;
  double start, stop;

  assert(MPI_Init(&argc, &argv) >= 0);
  assert(MPI_Comm_rank(MPI_COMM_WORLD, &rank) >= 0);

  fapl = H5Pcreate(H5P_FILE_ACCESS);
  assert(fapl >= 0);
  assert(H5Pset_libver_bounds(fapl, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST) >= 0);
  assert(H5Pset_fapl_core(fapl, incr, 1) >= 0);
  assert(H5Pset_core_write_tracking(fapl, 1, (size_t)PAGE_SIZE) >= 0);

  buf = (float*) malloc(X*Y*Z*sizeof(float));

  MPI_Barrier(MPI_COMM_WORLD);
  start = MPI_Wtime();

  for (iter = 0; iter < MAX_ITER; ++iter)
    {
      if (rank == 0)
        {
          printf("Iteration: %u\n", iter);
        }

      assert(sprintf(name, "rank%05dtime%04u.h5", rank, iter) > 0);
      file = H5Fcreate(name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
      assert(file >= 0);

      for (level = 0; level < MAX_TIME_LEVEL; ++level)
        {
          assert(sprintf(name, "level%03d", level) > 0);
          group = H5Gcreate2(file, name, H5P_DEFAULT, H5P_DEFAULT,
                             H5P_DEFAULT);
          assert(group >= 0);

          for (igroup = 0; igroup < MAX_GROUPS; ++igroup)
            {
              assert(sprintf(name, "group%02d", igroup) > 0);
              group1 = H5Gcreate2(group, name, H5P_DEFAULT, H5P_DEFAULT,
                                  H5P_DEFAULT);
              assert(group1 >= 0);

              for (idset = 0; idset < MAX_DATASETS; ++idset)
                {
                  assert(sprintf(name, "dataset%02d", idset) > 0);
                  assert(H5LTmake_dataset_float(group1, name, RANK, dims, buf)
                         >= 0);
                }

              assert(H5Gclose(group1) >= 0);
            }

          assert(H5Gclose(group) >= 0);
        }

      assert(H5Fclose(file) >= 0);
    }

  stop = MPI_Wtime();
  MPI_Barrier(MPI_COMM_WORLD);

  if (rank == 0)
    {
      printf("Total time: %f s\n", stop-start);
      printf("Aggregate bandwidth per process: %f GB/s\n",
             ((float)X*Y*Z*sizeof(float)) *
             ((float) MAX_DATASETS*MAX_GROUPS*MAX_TIME_LEVEL*MAX_ITER) / 
             (1024.0*1024.0*1024.0*(stop-start)));
    }

  free((void*) buf);

  assert(H5Pclose(fapl) >= 0);

  assert(MPI_Finalize() >= 0);

  return 0;
}
