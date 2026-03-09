/***************************************************************************//**
 * @note
 * Copyright (C) 2019-2022 MemryX Limited. All rights reserved.
 *
 ******************************************************************************/
#include "memx_common.h"
#include "memx_status.h"

#include "memx_test.h"
#include "memx_ringbuffer.h"

/***************************************************************************//**
 * testcase
 ******************************************************************************/
MEMX_TESTCASE(test_memx_ringbuffer_create)
{
  if(memx_test_result_okay) { // min_0: size=0 -> return nullptr
    MemxRingBuffer *ring_buffer = memx_ringbuffer_create(0);
    if(memx_test_assert_null(ring_buffer)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_CREATE_MIN_0_FAIL);
    }
    memx_ringbuffer_destroy(ring_buffer);
  }
  if(memx_test_result_okay) { // min_1: size=1 -> return ptr with size=1
    MemxRingBuffer *ring_buffer = memx_ringbuffer_create(1);
    if(memx_test_assert_not_null(ring_buffer)
      && memx_test_assert_not_null(ring_buffer->data)
      && memx_test_assert_equal(ring_buffer->size, 1)
      && memx_test_assert_equal(ring_buffer->raw_mask, 0x1)
      && memx_test_assert_equal(ring_buffer->ptr_mask, 0x0)
      && memx_test_assert_equal(ring_buffer->ptr_write, 0)
      && memx_test_assert_equal(ring_buffer->ptr_read, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_CREATE_MIN_1_FAIL);
    }
    memx_ringbuffer_destroy(ring_buffer);
  }
  if(memx_test_result_okay) { // mid_0: size=32 -> return ptr with size=32
    MemxRingBuffer *ring_buffer = memx_ringbuffer_create(32);
    if(memx_test_assert_not_null(ring_buffer)
      && memx_test_assert_not_null(ring_buffer->data)
      && memx_test_assert_equal(ring_buffer->size, 32)
      && memx_test_assert_equal(ring_buffer->raw_mask, 0x3f)
      && memx_test_assert_equal(ring_buffer->ptr_mask, 0x1f)
      && memx_test_assert_equal(ring_buffer->ptr_write, 0)
      && memx_test_assert_equal(ring_buffer->ptr_read, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_CREATE_MID_0_FAIL);
    }
    memx_ringbuffer_destroy(ring_buffer);
  }
  if(memx_test_result_okay) { // mid_1: size=8193 -> return ptr with size=8192
    MemxRingBuffer *ring_buffer = memx_ringbuffer_create(8193);
    if(memx_test_assert_not_null(ring_buffer)
      && memx_test_assert_not_null(ring_buffer->data)
      && memx_test_assert_equal(ring_buffer->size, 8192)
      && memx_test_assert_equal(ring_buffer->raw_mask, 0x3fff)
      && memx_test_assert_equal(ring_buffer->ptr_mask, 0x1fff)
      && memx_test_assert_equal(ring_buffer->ptr_write, 0)
      && memx_test_assert_equal(ring_buffer->ptr_read, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_CREATE_MID_1_FAIL);
    }
    memx_ringbuffer_destroy(ring_buffer);
  }
  if(memx_test_result_okay) { // max_0: size=2^31 -> return nullptr, do not test size=2^30 for it is too big
    MemxRingBuffer *ring_buffer = memx_ringbuffer_create(1<<31);
    if(memx_test_assert_null(ring_buffer)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_CREATE_MAX_0_FAIL);
    }
    memx_ringbuffer_destroy(ring_buffer);
  }

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_ringbuffer_put)
{
  uint8_t ifmap[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
  size_t length = 0;

  MemxRingBuffer *ring_buffer = memx_ringbuffer_create(16);
  if(memx_test_assert_not_null(ring_buffer)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_RINGBUFFER_PUT_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: ring_buffer=nullptr -> return 0
    length = memx_ringbuffer_put(NULL, ifmap, 16);
    if(memx_test_assert_equal(length, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PUT_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // null_1: source=nullptr -> return 0
    length = memx_ringbuffer_put(ring_buffer, NULL, 16);
    if(memx_test_assert_equal(length, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PUT_NULL_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: size=0 -> return 0
    ring_buffer->ptr_write = 0;
    ring_buffer->ptr_read = 0;
    memset(ring_buffer->data, 0, ring_buffer->size);
    length = memx_ringbuffer_put(ring_buffer, ifmap, 0);
    if(memx_test_assert_equal(length, 0)
      && memx_test_assert_equal(ring_buffer->ptr_write, 0)
      && memx_test_assert_equal(ring_buffer->ptr_read, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PUT_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: size=1 -> return 1
    ring_buffer->ptr_write = 0;
    ring_buffer->ptr_read = 0;
    memset(ring_buffer->data, 0, ring_buffer->size);
    length = memx_ringbuffer_put(ring_buffer, ifmap, 1);
    if(memx_test_assert_equal(length, 1)
      && memx_test_assert_equal(ring_buffer->ptr_write, 1)
      && memx_test_assert_equal(ring_buffer->ptr_read, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PUT_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_0: size=max+0 -> return max
    ring_buffer->ptr_write = 0;
    ring_buffer->ptr_read = 0;
    memset(ring_buffer->data, 0, ring_buffer->size);
    length = memx_ringbuffer_put(ring_buffer, ifmap, 16);
    if(memx_test_assert_equal(length, 16)
      && memx_test_assert_equal(ring_buffer->ptr_write, 16)
      && memx_test_assert_equal(ring_buffer->ptr_read, 0)
      && memx_test_assert_equal(ring_buffer->data[0], 0)
      && memx_test_assert_equal(ring_buffer->data[15], 15)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PUT_MAX_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_1: size=max+1 -> return max
    ring_buffer->ptr_write = 0;
    ring_buffer->ptr_read = 0;
    memset(ring_buffer->data, 0, ring_buffer->size);
    length = memx_ringbuffer_put(ring_buffer, ifmap, 17);
    if(memx_test_assert_equal(length, 16)
      && memx_test_assert_equal(ring_buffer->ptr_write, 16)
      && memx_test_assert_equal(ring_buffer->ptr_read, 0)
      && memx_test_assert_equal(ring_buffer->data[0], 0)
      && memx_test_assert_equal(ring_buffer->data[15], 15)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PUT_MAX_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // part_0: size=max -> return r-w
    ring_buffer->ptr_write = 0;
    ring_buffer->ptr_read = 24;
    memset(ring_buffer->data, 0, ring_buffer->size);
    length = memx_ringbuffer_put(ring_buffer, ifmap, 16);
    if(memx_test_assert_equal(length, 8)
      && memx_test_assert_equal(ring_buffer->ptr_write, 8)
      && memx_test_assert_equal(ring_buffer->ptr_read, 24)
      && memx_test_assert_equal(ring_buffer->data[7], 7)
      && memx_test_assert_equal(ring_buffer->data[8], 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PUT_PART_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // part_1: size=max -> return r-w
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 8;
    memset(ring_buffer->data, 0, ring_buffer->size);
    length = memx_ringbuffer_put(ring_buffer, ifmap, 16);
    if(memx_test_assert_equal(length, 8)
      && memx_test_assert_equal(ring_buffer->ptr_write, 24)
      && memx_test_assert_equal(ring_buffer->ptr_read, 8)
      && memx_test_assert_equal(ring_buffer->data[7], 7)
      && memx_test_assert_equal(ring_buffer->data[8], 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PUT_PART_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // ring_0: size=max -> return max
    ring_buffer->ptr_write = 8;
    ring_buffer->ptr_read = 8;
    memset(ring_buffer->data, 0, ring_buffer->size);
    length = memx_ringbuffer_put(ring_buffer, ifmap, 16);
    if(memx_test_assert_equal(length, 16)
      && memx_test_assert_equal(ring_buffer->ptr_write, 24)
      && memx_test_assert_equal(ring_buffer->ptr_read, 8)
      && memx_test_assert_equal(ring_buffer->data[7], 15)
      && memx_test_assert_equal(ring_buffer->data[8], 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PUT_RING_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // ring_1: size=max -> return max
    ring_buffer->ptr_write = 24;
    ring_buffer->ptr_read = 24;
    memset(ring_buffer->data, 0, ring_buffer->size);
    length = memx_ringbuffer_put(ring_buffer, ifmap, 16);
    if(memx_test_assert_equal(length, 16)
      && memx_test_assert_equal(ring_buffer->ptr_write, 8)
      && memx_test_assert_equal(ring_buffer->ptr_read, 24)
      && memx_test_assert_equal(ring_buffer->data[7], 15)
      && memx_test_assert_equal(ring_buffer->data[8], 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PUT_RING_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // full_0: size=max -> return 0
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 0;
    memset(ring_buffer->data, 0, ring_buffer->size);
    length = memx_ringbuffer_put(ring_buffer, ifmap, 16);
    if(memx_test_assert_equal(length, 0)
      && memx_test_assert_equal(ring_buffer->ptr_write, 16)
      && memx_test_assert_equal(ring_buffer->ptr_read, 0)
      && memx_test_assert_equal(ring_buffer->data[0], 0)
      && memx_test_assert_equal(ring_buffer->data[15], 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PUT_FULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // full_1: size=max -> return 0
    ring_buffer->ptr_write = 0;
    ring_buffer->ptr_read = 16;
    memset(ring_buffer->data, 0, ring_buffer->size);
    length = memx_ringbuffer_put(ring_buffer, ifmap, 16);
    if(memx_test_assert_equal(length, 0)
      && memx_test_assert_equal(ring_buffer->ptr_write, 0)
      && memx_test_assert_equal(ring_buffer->ptr_read, 16)
      && memx_test_assert_equal(ring_buffer->data[0], 0)
      && memx_test_assert_equal(ring_buffer->data[15], 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PUT_FULL_1_FAIL);
    }
  }

  memx_ringbuffer_destroy(ring_buffer); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_ringbuffer_get)
{
  uint8_t ofmap[16] = {0};
  size_t length = 0;

  MemxRingBuffer *ring_buffer = memx_ringbuffer_create(16);
  if(memx_test_assert_not_null(ring_buffer)) { // pre-condition
    for(size_t i=0; i<ring_buffer->size; ++i)
      ring_buffer->data[i] = (ring_buffer->size-1 - i) & 0xff;
  } else {
    memx_test_result_set(MEMX_TEST_RINGBUFFER_GET_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: ring_buffer=nullptr -> return 0
    length = memx_ringbuffer_get(NULL, ofmap, 16);
    if(memx_test_assert_equal(length, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_GET_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // null_1: target=nullptr -> return 0
    length = memx_ringbuffer_get(ring_buffer, NULL, 16);
    if(memx_test_assert_equal(length, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_GET_NULL_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: size=0 -> return 0
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 0;
    memset(ofmap, 0, ring_buffer->size);
    length = memx_ringbuffer_get(ring_buffer, ofmap, 0);
    if(memx_test_assert_equal(length, 0)
      && memx_test_assert_equal(ring_buffer->ptr_write, 16)
      && memx_test_assert_equal(ring_buffer->ptr_read, 0)
      && memx_test_assert_equal(ofmap[0], 0)
      && memx_test_assert_equal(ofmap[15], 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_GET_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: size=1 -> return 1
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 0;
    memset(ofmap, 0, ring_buffer->size);
    length = memx_ringbuffer_get(ring_buffer, ofmap, 1);
    if(memx_test_assert_equal(length, 1)
      && memx_test_assert_equal(ring_buffer->ptr_write, 16)
      && memx_test_assert_equal(ring_buffer->ptr_read, 1)
      && memx_test_assert_equal(ofmap[0], 15)
      && memx_test_assert_equal(ofmap[1], 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_GET_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_0: size=max -> return max
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 0;
    memset(ofmap, 0, ring_buffer->size);
    length = memx_ringbuffer_get(ring_buffer, ofmap, 16);
    if(memx_test_assert_equal(length, 16)
      && memx_test_assert_equal(ring_buffer->ptr_write, 16)
      && memx_test_assert_equal(ring_buffer->ptr_read, 16)
      && memx_test_assert_equal(ofmap[0], 15)
      && memx_test_assert_equal(ofmap[14], 1)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_GET_MAX_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_1: size=max+1 -> return max
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 0;
    memset(ofmap, 0, ring_buffer->size);
    length = memx_ringbuffer_get(ring_buffer, ofmap, 17);
    if(memx_test_assert_equal(length, 16)
      && memx_test_assert_equal(ring_buffer->ptr_write, 16)
      && memx_test_assert_equal(ring_buffer->ptr_read, 16)
      && memx_test_assert_equal(ofmap[0], 15)
      && memx_test_assert_equal(ofmap[14], 1)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_GET_MAX_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // part_0: size=max -> return w-r
    ring_buffer->ptr_write = 8;
    ring_buffer->ptr_read = 0;
    memset(ofmap, 0, ring_buffer->size);
    length = memx_ringbuffer_get(ring_buffer, ofmap, 16);
    if(memx_test_assert_equal(length, 8)
      && memx_test_assert_equal(ring_buffer->ptr_write, 8)
      && memx_test_assert_equal(ring_buffer->ptr_read, 8)
      && memx_test_assert_equal(ofmap[7], 8)
      && memx_test_assert_equal(ofmap[8], 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_GET_PART_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // part_1: size=max -> return w-r
    ring_buffer->ptr_write = 24;
    ring_buffer->ptr_read = 16;
    memset(ofmap, 0, ring_buffer->size);
    length = memx_ringbuffer_get(ring_buffer, ofmap, 16);
    if(memx_test_assert_equal(length, 8)
      && memx_test_assert_equal(ring_buffer->ptr_write, 24)
      && memx_test_assert_equal(ring_buffer->ptr_read, 24)
      && memx_test_assert_equal(ofmap[7], 8)
      && memx_test_assert_equal(ofmap[8], 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_GET_PART_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // ring_0: size=max -> return max
    ring_buffer->ptr_write = 24;
    ring_buffer->ptr_read = 8;
    memset(ofmap, 0, ring_buffer->size);
    length = memx_ringbuffer_get(ring_buffer, ofmap, 16);
    if(memx_test_assert_equal(length, 16)
      && memx_test_assert_equal(ring_buffer->ptr_write, 24)
      && memx_test_assert_equal(ring_buffer->ptr_read, 24)
      && memx_test_assert_equal(ofmap[7], 0)
      && memx_test_assert_equal(ofmap[8], 15)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_GET_RING_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // ring_1: size=max -> return max
    ring_buffer->ptr_write = 8;
    ring_buffer->ptr_read = 24;
    memset(ofmap, 0, ring_buffer->size);
    length = memx_ringbuffer_get(ring_buffer, ofmap, 16);
    if(memx_test_assert_equal(length, 16)
      && memx_test_assert_equal(ring_buffer->ptr_write, 8)
      && memx_test_assert_equal(ring_buffer->ptr_read, 8)
      && memx_test_assert_equal(ofmap[7], 0)
      && memx_test_assert_equal(ofmap[8], 15)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_GET_RING_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // empty_0: size=max -> return 0
    ring_buffer->ptr_write = 0;
    ring_buffer->ptr_read = 0;
    memset(ofmap, 0, ring_buffer->size);
    length = memx_ringbuffer_get(ring_buffer, ofmap, 16);
    if(memx_test_assert_equal(length, 0)
      && memx_test_assert_equal(ring_buffer->ptr_write, 0)
      && memx_test_assert_equal(ring_buffer->ptr_read, 0)
      && memx_test_assert_equal(ofmap[0], 0)
      && memx_test_assert_equal(ofmap[15], 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_GET_EMPTY_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // empty_1: size=max -> return 0
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 16;
    memset(ofmap, 0, ring_buffer->size);
    length = memx_ringbuffer_get(ring_buffer, ofmap, 16);
    if(memx_test_assert_equal(length, 0)
      && memx_test_assert_equal(ring_buffer->ptr_write, 16)
      && memx_test_assert_equal(ring_buffer->ptr_read, 16)
      && memx_test_assert_equal(ofmap[0], 0)
      && memx_test_assert_equal(ofmap[15], 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_GET_EMPTY_1_FAIL);
    }
  }

  memx_ringbuffer_destroy(ring_buffer); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_ringbuffer_drop)
{
  size_t length = 0;

  MemxRingBuffer *ring_buffer = memx_ringbuffer_create(16);
  if(memx_test_assert_not_null(ring_buffer)) { // pre-condition
    for(size_t i=0; i<ring_buffer->size; ++i)
      ring_buffer->data[i] = (ring_buffer->size-1 - i) & 0xff;
  } else {
    memx_test_result_set(MEMX_TEST_RINGBUFFER_DROP_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: ring_buffer=nullptr -> return 0
    length = memx_ringbuffer_drop(NULL, 16);
    if(memx_test_assert_equal(length, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_DROP_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: size=0 -> return 0
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 0;
    length = memx_ringbuffer_drop(ring_buffer, 0);
    if(memx_test_assert_equal(length, 0)
      && memx_test_assert_equal(ring_buffer->ptr_write, 16)
      && memx_test_assert_equal(ring_buffer->ptr_read, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_DROP_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: size=1 -> return 1
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 0;
    length = memx_ringbuffer_drop(ring_buffer, 1);
    if(memx_test_assert_equal(length, 1)
      && memx_test_assert_equal(ring_buffer->ptr_write, 16)
      && memx_test_assert_equal(ring_buffer->ptr_read, 1)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_DROP_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_0: size=max -> return max
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 0;
    length = memx_ringbuffer_drop(ring_buffer, 16);
    if(memx_test_assert_equal(length, 16)
      && memx_test_assert_equal(ring_buffer->ptr_write, 16)
      && memx_test_assert_equal(ring_buffer->ptr_read, 16)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_DROP_MAX_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_1: size=max+1 -> return max
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 0;
    length = memx_ringbuffer_drop(ring_buffer, 17);
    if(memx_test_assert_equal(length, 16)
      && memx_test_assert_equal(ring_buffer->ptr_write, 16)
      && memx_test_assert_equal(ring_buffer->ptr_read, 16)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_DROP_MAX_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // part_0: size=max -> return w-r
    ring_buffer->ptr_write = 8;
    ring_buffer->ptr_read = 0;
    length = memx_ringbuffer_drop(ring_buffer, 16);
    if(memx_test_assert_equal(length, 8)
      && memx_test_assert_equal(ring_buffer->ptr_write, 8)
      && memx_test_assert_equal(ring_buffer->ptr_read, 8)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_DROP_PART_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // part_1: size=max -> return w-r
    ring_buffer->ptr_write = 24;
    ring_buffer->ptr_read = 16;
    length = memx_ringbuffer_drop(ring_buffer, 16);
    if(memx_test_assert_equal(length, 8)
      && memx_test_assert_equal(ring_buffer->ptr_write, 24)
      && memx_test_assert_equal(ring_buffer->ptr_read, 24)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_DROP_PART_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // ring_0: size=max -> return max
    ring_buffer->ptr_write = 24;
    ring_buffer->ptr_read = 8;
    length = memx_ringbuffer_drop(ring_buffer, 16);
    if(memx_test_assert_equal(length, 16)
      && memx_test_assert_equal(ring_buffer->ptr_write, 24)
      && memx_test_assert_equal(ring_buffer->ptr_read, 24)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_DROP_RING_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // ring_1: size=max -> return max
    ring_buffer->ptr_write = 8;
    ring_buffer->ptr_read = 24;
    length = memx_ringbuffer_drop(ring_buffer, 16);
    if(memx_test_assert_equal(length, 16)
      && memx_test_assert_equal(ring_buffer->ptr_write, 8)
      && memx_test_assert_equal(ring_buffer->ptr_read, 8)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_DROP_RING_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // empty_0: size=max -> return 0
    ring_buffer->ptr_write = 0;
    ring_buffer->ptr_read = 0;
    length = memx_ringbuffer_drop(ring_buffer, 16);
    if(memx_test_assert_equal(length, 0)
      && memx_test_assert_equal(ring_buffer->ptr_write, 0)
      && memx_test_assert_equal(ring_buffer->ptr_read, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_DROP_EMPTY_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // empty_1: size=max -> return 0
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 16;
    length = memx_ringbuffer_drop(ring_buffer, 16);
    if(memx_test_assert_equal(length, 0)
      && memx_test_assert_equal(ring_buffer->ptr_write, 16)
      && memx_test_assert_equal(ring_buffer->ptr_read, 16)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_DROP_EMPTY_1_FAIL);
    }
  }

  memx_ringbuffer_destroy(ring_buffer); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_ringbuffer_peek_usage)
{
  size_t length = 0;

  MemxRingBuffer *ring_buffer = memx_ringbuffer_create(16);
  if(memx_test_assert_not_null(ring_buffer)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_USAGE_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: ring_buffer=nullptr -> return 0
    length = memx_ringbuffer_peek_usage(NULL);
    if(memx_test_assert_equal(length, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_USAGE_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // empty_0: w=r=0 -> return 0
    ring_buffer->ptr_write = 0;
    ring_buffer->ptr_read = 0;
    length = memx_ringbuffer_peek_usage(ring_buffer);
    if(memx_test_assert_equal(length, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_USAGE_EMPTY_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // empty_1: w=r=max -> return 0
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 16;
    length = memx_ringbuffer_peek_usage(ring_buffer);
    if(memx_test_assert_equal(length, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_USAGE_EMPTY_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // full_0: w=max,r=0 -> return max
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 0;
    length = memx_ringbuffer_peek_usage(ring_buffer);
    if(memx_test_assert_equal(length, 16)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_USAGE_FULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // full_1: w=0,r=max -> return max
    ring_buffer->ptr_write = 0;
    ring_buffer->ptr_read = 16;
    length = memx_ringbuffer_peek_usage(ring_buffer);
    if(memx_test_assert_equal(length, 16)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_USAGE_FULL_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // ring_0: w=max/2,r=max/2 -> return max
    ring_buffer->ptr_write = 24;
    ring_buffer->ptr_read = 8;
    length = memx_ringbuffer_peek_usage(ring_buffer);
    if(memx_test_assert_equal(length, 16)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_USAGE_RING_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // ring_1: w=max/2,r=max/2 -> return max
    ring_buffer->ptr_write = 8;
    ring_buffer->ptr_read = 24;
    length = memx_ringbuffer_peek_usage(ring_buffer);
    if(memx_test_assert_equal(length, 16)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_USAGE_RING_1_FAIL);
    }
  }

  memx_ringbuffer_destroy(ring_buffer); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_ringbuffer_peek_space)
{
  size_t length = 0;

  MemxRingBuffer *ring_buffer = memx_ringbuffer_create(16);
  if(memx_test_assert_not_null(ring_buffer)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_SPACE_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: ring_buffer=nullptr -> return 0
    length = memx_ringbuffer_peek_space(NULL);
    if(memx_test_assert_equal(length, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_SPACE_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // empty_0: w=r=0 -> return max
    ring_buffer->ptr_write = 0;
    ring_buffer->ptr_read = 0;
    length = memx_ringbuffer_peek_space(ring_buffer);
    if(memx_test_assert_equal(length, 16)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_SPACE_EMPTY_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // empty_1: w=r=max -> return max
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 16;
    length = memx_ringbuffer_peek_space(ring_buffer);
    if(memx_test_assert_equal(length, 16)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_SPACE_EMPTY_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // full_0: w=max,r=0 -> return 0
    ring_buffer->ptr_write = 16;
    ring_buffer->ptr_read = 0;
    length = memx_ringbuffer_peek_space(ring_buffer);
    if(memx_test_assert_equal(length, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_SPACE_FULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // full_1: w=0,r=max -> return 0
    ring_buffer->ptr_write = 0;
    ring_buffer->ptr_read = 16;
    length = memx_ringbuffer_peek_space(ring_buffer);
    if(memx_test_assert_equal(length, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_SPACE_FULL_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // ring_0: w=max/2,r=max/2 -> return 0
    ring_buffer->ptr_write = 24;
    ring_buffer->ptr_read = 8;
    length = memx_ringbuffer_peek_space(ring_buffer);
    if(memx_test_assert_equal(length, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_SPACE_RING_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // ring_1: w=max/2,r=max/2 -> return 0
    ring_buffer->ptr_write = 8;
    ring_buffer->ptr_read = 24;
    length = memx_ringbuffer_peek_space(ring_buffer);
    if(memx_test_assert_equal(length, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_RINGBUFFER_PEAK_SPACE_RING_1_FAIL);
    }
  }

  memx_ringbuffer_destroy(ring_buffer); // clean-up

} MEMX_TESTCASE_END;

/***************************************************************************//**
 * test sequence
 ******************************************************************************/
int main(int argc, char **argv)
{
  memx_test_result result = MEMX_TEST_PASS;

  if(memx_test_result_okay)
    result = test_memx_ringbuffer_create();
  if(memx_test_result_okay)
    result = test_memx_ringbuffer_put();
  if(memx_test_result_okay)
    result = test_memx_ringbuffer_get();
  if(memx_test_result_okay)
    result = test_memx_ringbuffer_drop();
  if(memx_test_result_okay)
    result = test_memx_ringbuffer_peek_usage();
  if(memx_test_result_okay)
    result = test_memx_ringbuffer_peek_space();

  unused(argc);
  unused(argv);
  return 0;
}

