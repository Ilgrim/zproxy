//
// Created by abdess on 7/4/18.
//
#pragma  once

#include <unistd.h>
#include <memory>
class SystemInfo {  
  ssize_t page_size;
  ssize_t l1_data_cache_size;
  ssize_t l2_cache_size;
  ssize_t l1_data_cache_line_size;
  ssize_t l2_cache_line_size;
  static std::shared_ptr<SystemInfo> instance;

 public:

  SystemInfo() {
    update();
  }

  static std::shared_ptr<SystemInfo> data() {
    if (instance == nullptr)
      instance = std::shared_ptr<SystemInfo>(new SystemInfo());
    return instance;
  }

  /**
   * return the number of bytes in a memory page.
   */
  inline ssize_t getSystemPageSize() const {
    return page_size;
  }

  /**
   * Get the the line length of the Level 1 data cache in bytes.
   */
  inline ssize_t getL1DataCacheLineSize() const {
    return l1_data_cache_line_size;
  }

  /**
 * Get the Level 1 data cache size in bytes.
 */
  inline ssize_t getL2DataCacheLineSize() const {
    return l2_cache_line_size;
  }

  /**
   * Get L1 data cache size in bytes
   */
  inline ssize_t getL1DataCacheSize() const {
    return l1_data_cache_size;
  }

  /**
 * Get L1 data cache size in bytes
 */
  inline ssize_t getL2DataCacheSize() const {
    return l2_cache_size;
  }

  /**
   * update system data
   */
  inline void update() {
    page_size = ::sysconf(_SC_PAGESIZE);
    l1_data_cache_size = ::sysconf(_SC_LEVEL1_DCACHE_SIZE);
    l1_data_cache_line_size = ::sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    l2_cache_size = ::sysconf(_SC_LEVEL2_CACHE_SIZE);
    l2_cache_line_size = ::sysconf(_SC_LEVEL2_CACHE_LINESIZE);
  }

};



