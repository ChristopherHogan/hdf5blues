#include "hdf5.h"
#include "hdf5_hl.h"
#include "mpi.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if USE_SPLIT
#include "H5FDhermes.h"
#endif

/* 512 MB increments for re-allocation */
#define INCREMENT 512*1024*1024
#define MAX_DATASETS 4
#define MAX_GROUPS 5
#define MAX_ITER 1
#define MAX_LEN 255
#define MAX_TIME_LEVEL 70
/* 64 MB page size */
#define PAGE_SIZE 64*1024*1024
/* dataset shape */
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
  extern char* optarg;
  extern int optind, optopt;
  int c, errflg, backflg, incrflg, nopagflg, pagflg, rank, nranks;
  hid_t fapl, file, group, group1, dset;
  size_t incr, page;
  unsigned iter, level, igroup, idset, maxiter;
  char name[MAX_LEN * 2];
  char g1name[MAX_LEN];
  char g2name[MAX_LEN];
  hsize_t dims[3] = { X, Y, Z };
  float *buf;
  double start, stop;
  double t1, t2;
  char cdum[MAX_LEN];
  int i, memtotal, memfree, buffers, cached, swapcached, active, inactive;
  float fdum;
  char path[MAX_LEN] = {'.', 0};
  char *vfd = NULL;
  size_t hermes_vfd_md_page_size = 0;
  size_t hermes_vfd_rd_page_size = 0;


  assert (MPI_Init (&argc, &argv) >= 0);
  assert (MPI_Comm_rank (MPI_COMM_WORLD, &rank) >= 0);
  assert (MPI_Comm_size (MPI_COMM_WORLD, &nranks) >= 0);

  /* parse arguments */

  backflg = errflg = incrflg = nopagflg = pagflg = 0;
  incr = INCREMENT;
  page = PAGE_SIZE;
  maxiter = MAX_ITER;

  while ((c = getopt(argc, argv, ":bf:ni:t:p:")) != -1)
    {
      switch (c)
      {
      case 'b':
        backflg++;
        break;
      case 'f':
        /* fprintf(stdout, "Writing file at path: %s\n", optarg); */
        strncpy(path, optarg, MAX_LEN);
        break;
      case 'i':
        incrflg++;
        incr = (size_t)atol(optarg);
        if (incr == 0)
          {
            fprintf(stderr,
                    "Option -%c requires a positive integer argument\n", optopt);
            errflg++;
          }
        break;
      case 'n':
        nopagflg++;
        break;
      case 'p':
        pagflg++;
        page = (size_t)atol(optarg);
        if (page == 0)
          {
            fprintf(stderr,
                    "Option -%c requires a positive integer argument\n", optopt);
            errflg++;
          }
        break;
      case 't':
        maxiter = (unsigned)atol(optarg);
        if (maxiter == 0)
          {
            fprintf(stderr,
                    "Option -%c requires a positive integer argument\n", optopt);
            errflg++;
          }
        break;
      case ':':       /* -i or -p without operand */
        fprintf(stderr,
                "Option -%c requires an operand\n", optopt);
        errflg++;
        break;
      case '?':
        fprintf(stderr, "Unrecognized option: -%c\n", optopt);
        errflg++;
        break;
      default:
        errflg++;
        break;
      }
    }
  if (errflg)
    {
      if (rank == 0)
        {
          fprintf(stderr, "usage: . . . ");

          fprintf(stderr, "usage: h5core [OPTIONS]\n");
          fprintf(stderr, "  OPTIONS\n");
          fprintf(stderr, "     -b      Write file to disk on exit\n");
          fprintf(stderr, "     -f P    Write file at absolute or relative path P\n");
          fprintf(stderr, "     -i I    Memory buffer increment size in bytes [default: 512 MB]\n");
          fprintf(stderr, "     -n      Disable write (to disk) paging\n");
          fprintf(stderr, "     -p P    Page size in bytes [default: 64 MB]\n");
          fprintf(stderr, "     -t T    Number of iterations [default: 5]\n");
          fprintf(stderr, "\n");
          fflush(stderr);
        }

      exit(2);
    }

  if ((backflg == 0) && (pagflg > 0))
    {
      if (rank == 0)
        {
          fprintf(stderr, "The -p has no effect without the -b option");
          fflush(stderr);
        }
    }

  if (pagflg && nopagflg)
    {
      if (rank == 0)
        {
          fprintf(stderr, "The -n and -p options are mutually exclusive.");
          fflush(stderr);
        }
      exit(3);
    }

  /* Let's go! */

  fapl = H5Pcreate (H5P_FILE_ACCESS);
  assert (fapl >= 0);
  assert (H5Pset_libver_bounds (fapl, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST) >= 0);

#ifdef USE_CORE
  vfd = "core";
  assert (H5Pset_fapl_core (fapl, incr, (hbool_t) backflg) >= 0);
  if (nopagflg == 0)  /* the user didn't disable paging */
    {
      assert (H5Pset_core_write_tracking (fapl, 1, page) >= 0);
    }
#elif USE_LOG
  assert (H5Pset_fapl_log (fapl, "log.out", H5FD_LOG_ALL, 1 * 1024 * 1024 * 1024) >= 0);
#elif USE_SPLIT
  vfd = "split";
  hid_t meta_fapl = H5Pcreate(H5P_FILE_ACCESS);
  assert(meta_fapl >= 0);
  hid_t raw_fapl = H5Pcreate(H5P_FILE_ACCESS);
  assert(raw_fapl >= 0);
  hermes_vfd_md_page_size = 4096;
  assert(H5Pset_fapl_hermes(meta_fapl, false, hermes_vfd_md_page_size) >= 0);
  hermes_vfd_rd_page_size = 5529600;
  assert(H5Pset_fapl_hermes(raw_fapl, false, hermes_vfd_rd_page_size) >= 0);
  assert(H5Pset_fapl_split(fapl, "-m.h5", meta_fapl, "-r.h5", raw_fapl) >= 0);
#else
  vfd = getenv("HDF5_DRIVER");
  if (vfd && strncmp(vfd, "hermes", 6) == 0) {
    char *driver_config = getenv("HDF5_DRIVER_CONFIG");
    char *saveptr = NULL;
    char* token = strtok_r(driver_config, " ", &saveptr);
    token = strtok_r(0, " ", &saveptr);
    sscanf(token, "%zu", &hermes_vfd_rd_page_size);
  }
#endif

  if (rank == 0)
    {
      /* printf("\n"); */
      /* printf("Write to disk: %s\n", (backflg > 0) ? "YES" : "NO"); */
      /* printf("Increment size: %ld [bytes]\n", incr); */

      if (nopagflg == 0)
        {
          /* printf("Page size: %ld [bytes]\n", page); */
        }
      else
        {
          /* printf("Page size: PAGING DISABLED!\n"); */
        }

      /* printf("Iterations: %d\n", maxiter); */
      /* printf("\n"); */
      fflush(stdout);
    }

  const float kData = 8.0f;
  const size_t kNumElements = X * Y * Z;
  const size_t kNumBytes = kNumElements * sizeof(float);
  buf = (float *) malloc (kNumBytes);
  for (size_t i = 0; i < kNumElements; ++i) {
    buf[i] = kData;
  }

  // Get basic memory info on each node, see how memory usage changes
  // after flushing to disk; do we end up back where we started?

  /* getmemory (&memtotal, &memfree, &buffers, &cached, &swapcached, &active, */
  /*            &inactive); */

  if (rank == 0)
    {
      /* fprintf (stdout, "BEFORE RUNNING:\n"); */
      /* fflush (stdout); */
    }
  for (i = 0; i < nranks; i++)
    {
      if (rank == i)
        {
          /* printmemory (rank, memtotal, memfree, buffers, cached, swapcached, */
          /*              active, inactive); */
        }
      MPI_Barrier (MPI_COMM_WORLD);
    }

  start = MPI_Wtime ();

  /* outer loop - simulated time */
  for (iter = 0; iter < maxiter; ++iter)
    {
      if (rank == 0)
        {
          /* printf ("Iteration: %u\n", iter); */
          /* assert (fflush (stdout) == 0); */
        }

      assert (sprintf (name, "%s/rank%05dtime%04u.h5", path, rank, iter) > 0);
      file = H5Fcreate (name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
      assert (file >= 0);

      t1 = MPI_Wtime ();

      /* time level */
      for (level = 0; level < MAX_TIME_LEVEL; ++level)
        {
          assert (sprintf (g1name, "level%03d", level) > 0);
          group = H5Gcreate2 (file, g1name, H5P_DEFAULT, H5P_DEFAULT,
                              H5P_DEFAULT);
          assert (group >= 0);

          /* group level */
          for (igroup = 0; igroup < MAX_GROUPS; ++igroup)
            {
              assert (sprintf (g2name, "group%02d", igroup) > 0);
              group1 = H5Gcreate2 (group, g2name, H5P_DEFAULT, H5P_DEFAULT,
                                   H5P_DEFAULT);
              assert (group1 >= 0);

              /* dataset level */
              for (idset = 0; idset < MAX_DATASETS; ++idset)
                {
                  assert (sprintf (name, "dataset%02d", idset) > 0);
                  assert (H5LTmake_dataset_float (group1, name, RANK, dims, buf) >= 0);

#ifdef DEBUG
                  assert (fprintf (stderr,
                                   "rank %i time %i Wrote dataset %s/%s/%s\n",
                                   rank, level, g1name, g2name, name));
                  assert (fflush (stderr) == 0);
#endif
                }

              assert (H5Gclose (group1) >= 0);
            }

          assert (H5Gclose (group) >= 0);
        }

      t2 = MPI_Wtime ();

      /* assert (fprintf (stdout, */
      /*                  "rank %04i Total time for buffering chunks to memory:\t %f seconds\n", */
      /*                  rank, t2 - t1)); */
      /* assert (fflush (stdout) == 0); */

      MPI_Barrier (MPI_COMM_WORLD);    // To keep output less jumbled

      /* getmemory (&memtotal, &memfree, &buffers, &cached, &swapcached, &active, */
      /*            &inactive); */

      if (rank == 0)
        {
          /* fprintf (stdout, "BEFORE FLUSHING:\n"); */
          /* fflush (stdout); */
        }

      for (i = 0; i < nranks; i++)
        {
          if (rank == i)
            {
              /* printmemory (rank, memtotal, memfree, buffers, cached, swapcached, */
              /*              active, inactive); */
            }
          MPI_Barrier (MPI_COMM_WORLD);
        }

      /* close the file */
      t1 = MPI_Wtime ();
      assert (H5Fclose (file) >= 0);
      t2 = MPI_Wtime ();

      /* getmemory (&memtotal, &memfree, &buffers, &cached, &swapcached, &active, */
      /*            &inactive); */

      if (rank == 0)
        {
          /* fprintf (stdout, "AFTER FLUSHING:\n"); */
          /* fflush (stdout); */
        }

      for (i = 0; i < nranks; i++)
        {
          if (rank == i)
            {
              /* printmemory (rank, memtotal, memfree, buffers, cached, swapcached, */
              /*              active, inactive); */
              /* assert (fprintf (stdout, "rank %04i Total time for flushing to disk:\t\t %10.2f seconds\n", rank, t2 - t1)); */
              /* assert (fflush (stderr) == 0); */
            }
          MPI_Barrier (MPI_COMM_WORLD);
        }
    }

  stop = MPI_Wtime ();
  MPI_Barrier (MPI_COMM_WORLD);

  if (rank == 0)
    {
      /*  Bandwdith (GiB/s)	VFD	Total time	Levels	Datasets	Groups	Iters	VFD Page size */

      float total_time = stop - start;
      float bandwidth = ((float) X * Y * Z * sizeof (float)) *
        ((float) MAX_DATASETS * MAX_GROUPS * MAX_TIME_LEVEL * MAX_ITER) /
        (1024.0 * 1024.0 * 1024.0 * (stop - start));

      printf("%f,%s,%f,%d,%d,%d,%d,%zu,%zu\n", bandwidth, vfd, total_time, MAX_TIME_LEVEL, MAX_DATASETS,
             MAX_GROUPS, MAX_ITER, hermes_vfd_md_page_size, hermes_vfd_rd_page_size);
    }

  free ((void *) buf);

  assert (H5Pclose (fapl) >= 0);

#if USE_SPLIT
  assert(H5Pclose(meta_fapl) >= 0);
  assert(H5Pclose(raw_fapl) >= 0);
#endif

  /* assert (MPI_Finalize () >= 0); */

  return 0;

}

/* void */
/* getmemory (int *memtotal, int *memfree, int *buffers, int *cached, */
/*            int *swapcached, int *active, int *inactive) */
/* { */
/*   FILE *fp_meminfo; */
/*   char cdum[MAX_LEN]; */
/*   assert ((fp_meminfo = fopen ("/proc/meminfo", "r")) != (FILE *) NULL); */
/*   int unused = fscanf (fp_meminfo, "%s %i %s", cdum, memtotal, cdum); */
/*   unused = fscanf (fp_meminfo, "%s %i %s", cdum, memfree, cdum); */
/*   unused = fscanf (fp_meminfo, "%s %i %s", cdum, buffers, cdum); */
/*   unused = fscanf (fp_meminfo, "%s %i %s", cdum, cached, cdum); */
/*   unused = fscanf (fp_meminfo, "%s %i %s", cdum, swapcached, cdum); */
/*   unused = fscanf (fp_meminfo, "%s %i %s", cdum, active, cdum); */
/*   unused = fscanf (fp_meminfo, "%s %i %s", cdum, inactive, cdum); */
/*   unused = fclose (fp_meminfo); */

/* } */


/* void */
/* printmemory (int rank, int memtotal, int memfree, int buffers, int cached, */
/*              int swapcached, int active, int inactive) */
/* { */

/*   float fdum; */
/*   float kibble = 1.024;    // 1024/1000, KiB/KB conversion */
/*   fdum = 1.0e-6 * (float) memtotal / kibble;   fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "Memtotal", fdum); */
/*   fdum = 1.0e-6 * (float) memfree / kibble;    fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "MemFree", fdum); */
/*   fdum = 1.0e-6 * (float) buffers / kibble;    fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "Buffers", fdum); */
/*   fdum = 1.0e-6 * (float) cached / kibble;     fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "Cached", fdum); */
/*   fdum = 1.0e-6 * (float) swapcached / kibble; fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "SwapCached", fdum); */
/*   fdum = 1.0e-6 * (float) active / kibble;     fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "Active", fdum); */
/*   fdum = 1.0e-6 * (float) inactive / kibble;   fprintf (stdout, "rank %04i %12s: %10.2f GB\n", rank, "Inactive", fdum); */
/*   assert (fflush (stderr) == 0); */

/* } */
