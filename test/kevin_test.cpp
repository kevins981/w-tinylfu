#include "../wtinylfu.hpp"
#include "../bloom_filter.hpp"
#include <iostream>
#include <cstdint>
#include <string>
#include <fstream>
#include <set>


// For num items = 262144, false positive rate = 0.001, 4 hash functions
// 5M is from bloom filter calculator. /16 is becuase each 64bit element holds 16 counters.
//#define NUM_ENTRIES 5000000/16
//#define CACHE_SIZE 262144

// For num items = 810k, false positive rate = 0.001, 4 hash functions
#define NUM_ENTRIES 17000000/16
#define CACHE_SIZE 810000

int main() {
    uint8_t hot_thresh = 10;

    frequency_sketch<uint64_t> lfu(NUM_ENTRIES);
    std::set<uint64_t> hot_pages;

    std::string line;
    std::ifstream access_file("accesses_huge.txt");
    if(!access_file.is_open()) {
      perror("Error open");
      exit(EXIT_FAILURE);
    }

    uint64_t count = 0;
    uint64_t stat_hits = 0;
    uint64_t stat_misses = 0;
    uint64_t stat_prev_hits = 0;
    uint64_t stat_prev_misses = 0;

    while(getline(access_file, line)) {
      // string to unsigned long long
      uint64_t page_num = std::stoull(line,nullptr,16) >> 12;
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

      count++;
      if (count % 5000000 == 0) {
        // Print stat for every 5M accesses.
        uint64_t incr_hits = stat_hits - stat_prev_hits;
        uint64_t incr_misses = stat_misses - stat_prev_misses;
        float hit_rate = (float)incr_hits/((float)incr_misses+(float)incr_hits)*100;
        //printf("hits %ld, misses %ld, hit rate %f%%. cache size %ld \n", incr_hits, incr_misses, 
        //            hit_rate, hot_pages.size());
        std::cout << incr_hits << "," << incr_misses << "," << hit_rate << "," << hot_pages.size() << std::endl;
        stat_prev_hits = stat_hits;
        stat_prev_misses = stat_misses;
      }

      //if (count == 262144*10) {
      //  count = 0;
      //  printf("=========== resetting");
      //  for (uint64_t page : pages) {
      //    if (lfu.frequency(page) >= 10) {
      //      printf(" %lx: %d \n", page, lfu.frequency(page));
      //    }
      //  }
      //}
    }

    printf("=========== done. print hot pages in cache (freq >= %d) \n", hot_thresh);
    for (uint64_t page : hot_pages) {
      printf("%lx \n", page);
    }

}
