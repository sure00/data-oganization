#ifndef BUFFER_MGN_ALGORITHM_H
#define BUFFER_MGN_ALGORITHM_H

#include "buffer_mgr_stat.h"
#include "storage_mgr.h"

// The algorithm for buffer management 
RC FIFO(BM_BufferPool *const bm, BM_PageHandle *const page, const PageNumber pageNum);
RC LRU(BM_BufferPool *const bm, BM_PageHandle *const page, const PageNumber pageNum);
RC CLOCK(BM_BufferPool *const bm, BM_PageHandle *const page, const PageNumber pageNum);
RC LFU(BM_BufferPool *const bm, BM_PageHandle *const page, const PageNumber pageNum);
RC LRU_K(BM_BufferPool *const bm, BM_PageHandle *const page, const PageNumber pageNum);
RC maintainSortedFrameList(BM_BufferPool *const bm, struct BM_PageFrame *selectedFrame);
RC maintainLFUFrameList(BM_BufferPool *const bm, struct BM_PageFrame *selectedFrame);
#endif
