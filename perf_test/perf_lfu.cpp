#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>
#include <sys/mman.h>
#include <cassert>
#include <pthread.h>
#include <cerrno>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <string>
#include <fstream>
#include <set>

#include "../wtinylfu.hpp"
#include "../bloom_filter.hpp"

// Perf related 
#define PERF_PAGES	(1 + (1 << 16))	// Has to be == 1+2^n, here 1MB
#define NPROC 64
//#define OUTFILE "perf_cpp.txt"

// TinyLFU related
#define NUM_ENTRIES 108000000/16
#define CACHE_SIZE 700000


struct perf_sample {
  struct perf_event_header header;
  __u64	ip;
  __u32 pid, tid;    /* if PERF_SAMPLE_TID */
  __u64 addr;        /* if PERF_SAMPLE_ADDR */
};

static long
perf_event_open(struct perf_event_attr *hw_event, pid_t pid,
                int cpu, int group_fd, unsigned long flags)
{
    int ret;
    ret = syscall(__NR_perf_event_open, hw_event, pid, cpu,
                   group_fd, flags);
    return ret;
}

void* perf_func(void*) {
//int main() {
    printf("getpid : %d \n", getpid());
    struct perf_event_attr pe;
    int fd[NPROC];
    static struct perf_event_mmap_page *perf_page[NPROC];
    uint64_t throttle_cnt = 0;
    uint64_t unthrottle_cnt = 0;
    uint64_t unknown_cnt = 0;
    uint64_t sample_cnt = 0;
    uint64_t nodata_cnt = 0;

    memset(&pe, 0, sizeof(pe));

    pe.type = PERF_TYPE_RAW;
    pe.size = sizeof(pe);
    //pe.config = 0x5301d3;
    pe.config = 0x1d3;
    //pe.config = 0x5320d1;
    //pe.config1 = 0;
    pe.sample_period = 200;
    //pe.sample_freq = 20000;
    pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_ADDR;
    pe.disabled = 0;
    pe.freq = 0;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 0;
    pe.exclude_callchain_kernel = 1;
    //pe.exclude_callchain_user = 1;
    pe.precise_ip = 1;
    pe.inherit = 1; 
    pe.task = 1; 
    pe.sample_id_all = 1;

    // perf_event_open args: perf_event_attr, pid, cpu, group_fd, flags.
    // pid == 0 && cpu == -1: measures the calling process/thread on any CPU.
    // returns a file descriptor, for use in subsequent system calls.
    //fd = perf_event_open(&pe, 0, -1, -1, 0);
    for (int i = 0; i < NPROC; i++) {
      fd[i] = perf_event_open(&pe, -1, i, -1, 0);
      if (fd[i] == -1) {
         std::perror("failed");
         fprintf(stderr, "Error opening leader %llx\n", pe.config);
         exit(EXIT_FAILURE);
      }

      size_t mmap_size = sysconf(_SC_PAGESIZE) * PERF_PAGES;

      // mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
      // prot: protection. How the page may be used.
      // flags: whether updates to the mapping are visible to other processes mapping the same region.
      // fd: file descriptor.
      // offset: offset into the file.
      perf_page[i] = (perf_event_mmap_page *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd[i], 0);
      if(perf_page[i] == MAP_FAILED) {
        perror("mmap");
      }
      assert(perf_page[i] != MAP_FAILED);
    }

    std::cout << "1start perf recording." << std::endl;

    //std::ofstream outfile;
    //outfile.open(OUTFILE);
    //std::cout << "Writing perf results to " << OUTFILE << std::endl;

    // setup TinyLFU
    uint8_t hot_thresh = 8;
    frequency_sketch<uint64_t> lfu(NUM_ENTRIES);
    std::set<uint64_t> hot_pages;

    uint64_t stat_hits = 0;
    uint64_t stat_misses = 0;
    uint64_t stat_prev_hits = 0;
    uint64_t stat_prev_misses = 0;
    uint64_t page_num;
    uint64_t incr_hits;
    uint64_t incr_misses;
    float hit_rate;
    // start perf monitoring.
    for(;;){
      for (int i = 0; i < NPROC; i++) {
        struct perf_event_mmap_page *p = perf_page[i];
        char *pbuf = (char *)p + p->data_offset;
        __sync_synchronize();

        // this probably keeps looping if no new perf data is collected. 
        // interrupt might be a better idea.
        if(p->data_head == p->data_tail) {
          //std::cout << "no data." << std::endl;
          nodata_cnt++;
          continue;
        }
        struct perf_event_header *ph = (perf_event_header *)((void *)(pbuf + (p->data_tail % p->data_size)));
        struct perf_sample* ps;
        switch(ph->type) {
          case PERF_RECORD_SAMPLE:
            //printf(" [DEBUG]: %llx , %d \n", ps->addr, ps->pid);
            ps = (struct perf_sample*)ph;
            assert(ps != NULL);
            //std::cout << std::hex << ps->addr << std::endl;
            //outfile << std::hex << ps->addr << std::endl;
            page_num = ps->addr >> 12; // get virtual page number from address
            lfu.record_access(page_num);
            if (hot_pages.count(page_num) == 1) {
              // Page present in fast memory. Hit.
              stat_hits++;
            } else {
              // Miss. Check if we should promote it.
              if (lfu.frequency(page_num) >= hot_thresh && hot_pages.size() <= CACHE_SIZE) {
                hot_pages.insert(page_num);
              }
              stat_misses++;
            }

            sample_cnt++;
            if (sample_cnt % 100000 == 0){
              // Print stat
              incr_hits = stat_hits - stat_prev_hits;
              incr_misses = stat_misses - stat_prev_misses;
              hit_rate = (float)incr_hits/((float)incr_misses+(float)incr_hits)*100;
              //printf("hits %ld, misses %ld, hit rate %f%%. cache size %ld \n", incr_hits, incr_misses,
              //            hit_rate, hot_pages.size());
              std::cout << incr_hits << "," << incr_misses << "," << hit_rate << "," << hot_pages.size() << std::endl;
              stat_prev_hits = stat_hits;
              stat_prev_misses = stat_misses;
              //std::cout << "samples: " << sample_cnt <<  ". no data " << nodata_cnt  << std::endl;
            }
            break;
          case PERF_RECORD_THROTTLE:
            throttle_cnt++;
            break;
          case PERF_RECORD_UNTHROTTLE:
            unthrottle_cnt++;
            break;
          default:
            //fprintf(stderr, "Unknown type %u\n", ph->type);
            unknown_cnt++;
            break;
        }
        // When the mapping is PROT_WRITE, the data_tail value should
        // be written by user space to reflect the last read data.
        // In this case, the kernel will not overwrite unread data.
        p->data_tail += ph->size;
      }
    }
  //outfile.close();
  return NULL;
}

