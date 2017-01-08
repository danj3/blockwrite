#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/file.h>

#include <popt.h>

struct scaling { long base; off_t denom; char suffix; long long bytes; };

double
tdelta(struct timeval t1, struct timeval t2) {
  struct timeval delta;
  double deltad;

  delta.tv_sec=t2.tv_sec-t1.tv_sec;
  if ( (delta.tv_usec=t2.tv_usec-t1.tv_usec) < 0 ) {
   delta.tv_sec--;
   delta.tv_usec+=1000000;
  }
  deltad=delta.tv_sec + ((double)delta.tv_usec*0.000001);
  return deltad;
}

static off_t
scale(char *sdata, struct scaling *scale) {
  char *endptr;
  scale->base=strtol(sdata,&endptr,0);
  if ( *endptr != '\0' ) {
    switch (*endptr) {
    case 'B':
      scale->denom=1;
      break;
    case 'K':
      scale->denom=1024;
      break;
    case 'M':
      scale->denom=1024*1024;
      break;
    case 'G':
      scale->denom=1024*1024*1024;
      break;
    default:
      fprintf(stderr,"Unknown scale %c\n",*endptr);
      return 0;
    }
    scale->suffix=*endptr;
  }
  return scale->bytes=((scale->base)*(scale->denom));
}

int
main(int argc, const char **argv) {
  off_t block_bytes=8192,fsync_count=0;
  long count=1,bytes=0;
  char *filename=0,*filename_alt=0;
  char *datasz_s=0,*blocksz_s=0,*name=0,*config=0;
  int fd;
  int fsync_freq=0, stride=0, stride_off=0, fsync_dset=0;
  int read_instead=0, read_backward=0;
  char *obuf=0;
  long long cblock=0, allblock=0;
  double fsync_aggr=0.0;
  int i, namelen=0;
  char c;
  int open_trunc=0;
  int forks=0, child=0;
  int pid_status;
  int tmpfd=0;
  char tmppath[255];

  struct timeval t1,t2,t3,f1,f2;
  double deltad;

  struct scaling
    scale_o={.denom=1, .suffix='B', .base=1},
    scale_b={.denom=1024, .suffix='K', .base=8},
      scale_d={.denom=1024, .suffix='K', .base=8};

  poptContext optCon;
  struct poptOption  optionsTable[] = {
    {"blocksz",'b', POPT_ARG_STRING, &blocksz_s, 'b', "write block size"},
    {"count",'c',POPT_ARG_INT, &count, 'c', "count of blocks to write"},
    {"datasz",'d',POPT_ARG_STRING, &datasz_s, 'd',
     "data qty to write (scale sensitive)"},

    {"ffreq",'f',POPT_ARG_INT, &fsync_freq, 0, "fsync frequency"},
    {"fsync-dset",'F',POPT_ARG_INT, &fsync_dset, 0, "fsyncs per dataset"},
    {"output",'o',POPT_ARG_STRING, &filename, 0, "output file or device"},
    {"name",'n',POPT_ARG_STRING, &name, 0, "Test name"},
    {"config",'C',POPT_ARG_STRING, &config, 0,
     "Config name (no spaces, colons)"},

    {"forks",'p',POPT_ARG_INT, &forks, 0,"Parallel forks doing same work"},
    {"kb",'K',POPT_ARG_NONE, 0, 'K', "output KByte scaling"},
    {"mb",'M',POPT_ARG_NONE, 0, 'M', "output MByte scaling"},
    {"gb",'G',POPT_ARG_NONE, 0, 'G', "output GByte scaling"},
    {"stride",'s',POPT_ARG_INT, &stride, 0,
     "Stride data to defeat rewrite caching"},
    {"read",'r',POPT_ARG_NONE, &read_instead, 0, "read instead of write"},
    {"read-back",'R',POPT_ARG_NONE, &read_backward, 0,
     "read backward, combine with -r"},
    {"trunc",'t',POPT_ARG_NONE, 0, 't', "truncate output file"},

    POPT_AUTOHELP
    POPT_TABLEEND
  };

  block_bytes=scale_b.base*scale_b.denom;

  optCon = poptGetContext(NULL,argc,argv,optionsTable,0);
  while ((c=poptGetNextOpt(optCon)) >= 0) {
    switch (c) {
    case 'K':
      scale_o=(struct scaling){.denom=1024, .suffix=c, .base=1};
      break;
    case 'M':
      scale_o=(struct scaling){.denom=1024*1024, .suffix=c, .base=1};
      break;
    case 'G':
      scale_o=(struct scaling){.denom=1024*1024*1024, .suffix=c, .base=1};
      break;
    case 'b':
      if ( (block_bytes=scale(blocksz_s,&scale_b)) == 0)
        return 10;
      break;
    case 'd':
      if ( (bytes=scale(datasz_s,&scale_d)) == 0) {
        if ( read_instead ) {
          struct stat statbuf;
          stat(filename,&statbuf);
          bytes=statbuf.st_size;
        } else {
          return 10;
        }
      }
      count=bytes/block_bytes;
      break;
    case 'c':
      scale_d.base=(count*block_bytes)/(long)scale_d.denom;
      break;
    case 't':
      open_trunc=O_TRUNC;
      break;
    default:
      fprintf(stderr,"Why am I here %c\n",c);
      return 10;
    }
  }

  bytes=(count*block_bytes);
  if ( fsync_dset > count )
    fsync_freq=count;
  else if ( fsync_dset > 0 )
    fsync_freq=count/fsync_dset;

  if ( fsync_freq == 0 )
    fsync_freq=count;

  if (filename==NULL) {
    fprintf(stderr,"output is required\n");
    return 10;
  }

  sprintf(tmppath,"/tmp/bw.%d",getpid());
  if ( (tmpfd=open(tmppath,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC)) < 0 ) {
    perror("open failed");
    exit(3);
  }
  if ( unlink(tmppath) < 0 ) {
    perror("unlink failed");
    exit(4);
  }

  if ( name==NULL ) {
    name=(char *) calloc(1,
                         (namelen=(1024 + (config==NULL?0:strlen(config)) ) ));
    sprintf(name,
            "%c:%s:bs=%lld%c-"
            "ds=%ld%c-"
            "ff=%d",
            (read_instead ? 'R' : 'W'),
            (config ? config : ""),
            (long long int)(block_bytes/scale_b.denom),
            scale_b.suffix,
            scale_d.base,
            scale_d.suffix,
            fsync_freq);
  }

  obuf = (char *) calloc(1,(size_t)(block_bytes*2));
  srand(block_bytes);
  for (i=0;i<block_bytes*2;i++) {
    obuf[i]=(char)rand();
  }
  strcpy(obuf,"BuFfErBeGiN");

  if (forks > 0) {
    while (forks > 0) {
      if ( (child=fork()) == 0 ) {
        filename_alt=malloc(strlen(filename)+8);
        sprintf(filename_alt,"%s.%d",filename,getpid());
        filename=filename_alt;
        goto process;
      }
      forks--;
    }
    while (wait(&pid_status)>0) {
    }
    exit(0);
  }

  process:
  if ( (fd=open(filename,
                (read_instead ? O_RDONLY : O_WRONLY|O_CREAT|open_trunc),
                0666)) < 0 ) {

    fprintf(stderr,"Unable to open %s to create and write (or read) %s\n",
        filename,strerror(errno));
    return 3;
  }

  gettimeofday(&t1,NULL);

  if ( read_instead ) {
    if ( read_backward ) {
      off_t pos=(off_t)bytes;
      for (; count > 0 ; count-- ) {
        pos-=block_bytes;
        lseek(fd,pos,SEEK_SET);
        if ((cblock=read(fd,obuf,(size_t)block_bytes)) < block_bytes) {
          fprintf(stdout,"Short read %lld\n",cblock);
          break;
        }
        allblock+=cblock;
      }
    } else {
      for (; count > 0 ; count-- ) {
        if ((cblock=read(fd,obuf,(size_t)block_bytes)) < block_bytes) {
          fprintf(stdout,"Short read %lld\n",cblock);
          break;
        }
        allblock+=cblock;
      }
    }
    gettimeofday(&t3,NULL);
  } else {
    for (; count > 0 ; count-- ) {
      if ( stride )
        stride_off=(count+stride)%block_bytes;

      if ( (cblock=write(fd,obuf+stride_off,(size_t)block_bytes)) <
           block_bytes ) {

        fprintf(stdout,"Short write %lld!\n",cblock);
        goto nomore;
        break;
      }
      allblock+=cblock;
      if ( (count%fsync_freq) == 0 ) {
        fsync_count++;
        gettimeofday(&f1,NULL);
        fsync(fd);
        gettimeofday(&f2,NULL);
        fsync_aggr+=tdelta(f1,f2);
      }
    }
    if ( allblock < scale_d.bytes ) {
      if ( stride )
        stride_off=(count+stride)%block_bytes;
      if ( (cblock=write(fd,obuf+stride_off,(size_t)(scale_d.bytes-allblock))) <
           (scale_d.bytes-allblock) ) {
        fprintf(stdout,"Short write %lld!\n",cblock);
      }
      allblock+=cblock;
    }

    nomore:

    gettimeofday(&t3,NULL);
    fsync(fd);
    gettimeofday(&f2,NULL);
    fsync_aggr+=tdelta(t3,f2);
    fsync_count++;
  }

  close(fd);

  gettimeofday(&t2,NULL);

  flock(tmpfd,LOCK_EX);

  fprintf(stdout,"%s %s fd=%d\n"
          " %s-count=%ld\n blocking=%lld%c\n"
          " datasz=%ld%c\n bytes=%ld\n"
          " fsync-freq=%d\n fsync/file=%ld\n"
          " stride-off=%d\n"
          ,
          name,
          filename,fd,
          (read_instead ? "read" : "write"),
          count,
          (long long int)(block_bytes/scale_b.denom),
          scale_b.suffix,
          scale_d.base,
          scale_d.suffix,
          bytes,
          fsync_freq,
          (fsync_freq>0 ? count/fsync_freq : 0),
          stride
          );
  fprintf(stdout," %s=%lld%c\n",
	  (read_instead ? "read" : "wrote"),
          allblock/scale_o.denom,scale_o.suffix);

  deltad=tdelta(t1,t2);
  fprintf(stdout," time=%fs\n",deltad);

  if ( deltad > 0 )
    fprintf(stdout," rate=%f%c/s\n",
            (allblock/scale_o.denom)/deltad,scale_o.suffix);

  fprintf(stdout," fsync-called=%lld\n"
          " fsync-loc2al-time=%fs\n",
          (long long int)fsync_count,
	  fsync_aggr);

  fprintf(stdout," fsync-final-t=%fs\n\n",tdelta(t3,t2));
  fflush(stdout);
  flock(tmpfd,LOCK_UN);
  return 0;
}
