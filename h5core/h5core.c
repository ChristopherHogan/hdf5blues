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

void getmemory (int *memtotal, int *memfree, int *buffers, int *cached,
                int *swapcached, int *active, int *inactive);

void printmemory (int rank, int memtotal, int memfree, int buffers, int cached,
                  int swapcached, int active, int inactive);

int
main (int argc, char **argv)
{
  int rank, nranks;
  hid_t fapl, file, group, group1, dset;
  size_t incr = INCREMENT;
  unsigned iter, level, igroup, idset;
  char name[MAX_LEN];
  char g1name[MAX_LEN];
  char g2name[MAX_LEN];
  hsize_t dims[3] = { X, Y, Z };
  float *buf;
  double start, stop;
  double t1, t2;
  char cdum[MAX_LEN];
  int i, memtotal, memfree, buffers, cached, swapcached, active, inactive;
  float fdum;

  assert (MPI_Init (&argc, &argv) >= 0);
  assert (MPI_Comm_rank (MPI_COMM_WORLD, &rank) >= 0);
  assert (MPI_Comm_size (MPI_COMM_WORLD, &nranks) >= 0);

  fapl = H5Pcreate (H5P_FILE_ACCESS);
  assert (fapl >= 0);
  assert (H5Pset_libver_bounds (fapl, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST) >= 0);
  assert (H5Pset_fapl_core (fapl, incr, 1) >= 0);
  assert (H5Pset_core_write_tracking (fapl, 1, (size_t) PAGE_SIZE) >= 0);

  buf = (float *) malloc (X * Y * Z * sizeof (float));

  // Get basic memory info on each node, see how memory usage changes
  // after flushing to disk; do we end up back where we started?

  getmemory (&memtotal, &memfree, &buffers, &cached, &swapcached, &active,
             &inactive);

  if (rank == 0)
    {
      fprintf (stdout, "BEFORE RUNNING:\n");
      fflush (stdout);
    }
  for (i = 0; i < nranks; i++)
    {
      if (rank == i)
        {
          printmemory (rank, memtotal, memfree, buffers, cached, swapcached,
                       active, inactive);
        }
      MPI_Barrier (MPI_COMM_WORLD);
    }

  start = MPI_Wtime ();

  for (iter = 0; iter < MAX_ITER; ++iter)
    {
      if (rank == 0)
        {
          printf ("Iteration: %u\n", iter);
          assert (fflush (stdout) == 0);
        }

      assert (sprintf (name, "rank%05dtime%04u.h5", rank, iter) > 0);
      file = H5Fcreate (name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
      assert (file >= 0);

      t1 = MPI_Wtime ();

      for (level = 0; level < MAX_TIME_LEVEL; ++level)
        {
          assert (sprintf (g1name, "level%03d", level) > 0);
          group = H5Gcreate2 (file, g1name, H5P_DEFAULT, H5P_DEFAULT,
                              H5P_DEFAULT);
          assert (group >= 0);

          for (igroup = 0; igroup < MAX_GROUPS; ++igroup)
            {
              assert (sprintf (g2name, "group%02d", igroup) > 0);
              group1 = H5Gcreate2 (group, g2name, H5P_DEFAULT, H5P_DEFAULT,
                                   H5P_DEFAULT);
              assert (group1 >= 0);

              for (idset = 0; idset < MAX_DATASETS; ++idset)
                {
                  assert (sprintf (name, "dataset%02d", idset) > 0);
                  assert (H5LTmake_dataset_float (group1, name, RANK, dims, buf) >= 0);
                  assert (fprintf (stderr, "rank %i time %i Wrote dataset %s/%s/%s\n", rank, level, g1name, g2name, name));
                  assert (fflush (stderr) == 0);
                }

              assert (H5Gclose (group1) >= 0);
            }

          assert (H5Gclose (group) >= 0);
        }
      t2 = MPI_Wtime ();
      assert (fprintf (stdout, "rank %04i Total time for buffering chunks to memory:\t %f seconds\n", rank, t2 - t1));
      assert (fflush (stdout) == 0);
      MPI_Barrier (MPI_COMM_WORLD);    // To keep output less jumbled

      getmemory (&memtotal, &memfree, &buffers, &cached, &swapcached, &active,
                 &inactive);

      if (rank == 0)
        {
          fprintf (stdout, "BEFORE FLUSHING:\n");
          fflush (stdout);
        }

      for (i = 0; i < nranks; i++)
        {
          if (rank == i)
            {
              printmemory (rank, memtotal, memfree, buffers, cached, swapcached,
                           active, inactive);
            }
          MPI_Barrier (MPI_COMM_WORLD);
        }

      t1 = MPI_Wtime ();
      assert (H5Fclose (file) >= 0);
      t2 = MPI_Wtime ();

      getmemory (&memtotal, &memfree, &buffers, &cached, &swapcached, &active,
                 &inactive);

      if (rank == 0)
        {
          fprintf (stdout, "AFTER FLUSHING:\n");
          fflush (stdout);
        }

      for (i = 0; i < nranks; i++)
        {
          if (rank == i)
            {
              printmemory (rank, memtotal, memfree, buffers, cached, swapcached,
                           active, inactive);
              assert (fprintf (stdout, "rank %04i Total time for flushing to disk:\t\t %10.2f seconds\n", rank, t2 - t1));
              assert (fflush (stderr) == 0);
            }
          MPI_Barrier (MPI_COMM_WORLD);
        }
    }

  stop = MPI_Wtime ();
  MPI_Barrier (MPI_COMM_WORLD);

  if (rank == 0)
    {
      printf ("Total time: %f s\n", stop - start);
      printf ("Aggregate bandwidth per process: %f GB/s\n",
              ((float) X * Y * Z * sizeof (float)) *
              ((float) MAX_DATASETS * MAX_GROUPS * MAX_TIME_LEVEL * MAX_ITER) /
              (1024.0 * 1024.0 * 1024.0 * (stop - start)));
    }

  free ((void *) buf);

  assert (H5Pclose (fapl) >= 0);

  assert (MPI_Finalize () >= 0);

  return 0;

}

void
getmemory (int *memtotal, int *memfree, int *buffers, int *cached,
           int *swapcached, int *active, int *inactive)
{
  FILE *fp_meminfo;
  char cdum[MAX_LEN];
  assert ((fp_meminfo = fopen ("/proc/meminfo", "r")) != (FILE *) NULL);
  fscanf (fp_meminfo, "%s %i %s", cdum, memtotal, cdum);
  fscanf (fp_meminfo, "%s %i %s", cdum, memfree, cdum);
  fscanf (fp_meminfo, "%s %i %s", cdum, buffers, cdum);
  fscanf (fp_meminfo, "%s %i %s", cdum, cached, cdum);
  fscanf (fp_meminfo, "%s %i %s", cdum, swapcached, cdum);
  fscanf (fp_meminfo, "%s %i %s", cdum, active, cdum);
  fscanf (fp_meminfo, "%s %i %s", cdum, inactive, cdum);
  fclose (fp_meminfo);

}


void
printmemory (int rank, int memtotal, int memfree, int buffers, int cached,
             int swapcached, int active, int inactive)
{

  float fdum;
  float kibble = 1.024;    // 1024/1000, KiB/KB conversion
  fdum = 1.0e-6 * (float) memtotal / kibble;   fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "Memtotal", fdum);
  fdum = 1.0e-6 * (float) memfree / kibble;    fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "MemFree", fdum);
  fdum = 1.0e-6 * (float) buffers / kibble;    fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "Buffers", fdum);
  fdum = 1.0e-6 * (float) cached / kibble;     fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "Cached", fdum);
  fdum = 1.0e-6 * (float) swapcached / kibble; fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "SwapCached", fdum);
  fdum = 1.0e-6 * (float) active / kibble;     fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "Active", fdum);
  fdum = 1.0e-6 * (float) inactive / kibble;   fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "Inactive", fdum);
  assert (fflush (stderr) == 0);

}
