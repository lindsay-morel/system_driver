/***************************************************************************//**
 * @note
 * Copyright (C) 2019-2022 MemryX Limited. All rights reserved.
 *
 ******************************************************************************/
#include <stdlib.h>
#include <string.h>

#include "memx_common.h"
#include "memx_status.h"
#include "memx_list.h"

#include "memx_test.h"
#include "memx_mpuio.h"
#include "memx_uart2ahb.h"

/***************************************************************************//**
 * interface stub function
 ******************************************************************************/
typedef struct _uart_record_t
{
  const void* data;
  int length;
} _uart_record_t;
static MemxList* _uart_write_records = NULL;
static MemxList* _uart_read_records = NULL;

#define _uart_record_destroy(_rec_) \
  do { \
    if((_rec_) != NULL) \
      free(_rec_); \
  } while(0)
#define _uart_records_destroy_all() \
  do { \
    void *_rec_ = NULL; \
    while((_rec_ = memx_list_pop(_uart_write_records)) != NULL) \
      free(_rec_); \
    while((_rec_ = memx_list_pop(_uart_read_records)) != NULL) \
      free(_rec_); \
  } while(0)

extern int _uart_get_baudrate_code(int baudrate)
{
  unused(baudrate);
  return 1;
}
extern int _uart_open_device(const char* device, int baudrate, int timeout)
{
  unused(device);
  unused(baudrate);
  unused(timeout);
  return 1;
}
extern int _uart_close_device(int fd)
{
  unused(fd);
  return 0;
}
extern int _uart_write(int fd, const void* data, int length)
{
  _uart_record_t* rec = (_uart_record_t*)malloc(sizeof(_uart_record_t));
  rec->data = data;
  rec->length = length;
  memx_list_push(_uart_write_records, rec);
  unused(fd);
  return length;
}
extern int _uart_read(int fd, void* data, int length)
{
  _uart_record_t* rec = (_uart_record_t*)malloc(sizeof(_uart_record_t));
  rec->data = data;
  rec->length = length;
  memx_list_push(_uart_read_records, rec);
  for(int i=0; i<length; ++i)
    ((uint8_t*)data)[i] = i & 0xff;
  unused(fd);
  return length;
}

/***************************************************************************//**
 * testcase
 ******************************************************************************/
MEMX_TESTCASE(test_memx_uart2ahb_create)
{
  if(memx_test_result_okay) { // null_0: device=nullptr -> return nullptr
    MemxMpuIo* mpuio = memx_uart2ahb_create(NULL, 921600, 100);
    if(memx_test_assert_null(mpuio)) {
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CREATE_NULL_0_FAIL);
    }
    memx_mpuio_destroy(mpuio);
  }
  if(memx_test_result_okay) { // mid_0: device=uart -> return ptr
    MemxMpuIo* mpuio = memx_uart2ahb_create("/dev/ttyUSB0", 921600, 100);
    if(memx_test_assert_not_null(mpuio)
      && memx_test_assert_not_null(mpuio->context)
      && memx_test_assert_equal(mpuio->destroy, (memx_mpuio_destroy_cb)&memx_uart2ahb_destroy)
      && memx_test_assert_equal(mpuio->control_write, (memx_mpuio_control_write_cb)&memx_uart2ahb_control_write)
      && memx_test_assert_equal(mpuio->control_read, (memx_mpuio_control_read_cb)&memx_uart2ahb_control_read)
      && memx_test_assert_equal(mpuio->stream_write, (memx_mpuio_stream_write_cb)&memx_uart2ahb_stream_write)
      && memx_test_assert_equal(mpuio->stream_read, (memx_mpuio_stream_read_cb)&memx_uart2ahb_stream_read)) {
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CREATE_MID_0_FAIL);
    }
    memx_mpuio_destroy(mpuio);
  }

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_uart2ahb_control_write)
{
  memx_status status = MEMX_STATUS_OK;
  uint8_t ifmap[1024] = {0};
  int transferred = 0;

  MemxMpuIo* mpuio = memx_uart2ahb_create("/dev/ttyUSB0", 921600, 100);
  if(memx_test_assert_not_null(mpuio)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: context=nullptr -> report error
    _uart_records_destroy_all();
    status = mpuio->control_write(NULL, 1, 0xff, ifmap, 16, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_UART2AHB_INVALID_CONTEXT)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 0)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // null_1: data=nullptr -> report error
    _uart_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xff, NULL, 16, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_UART2AHB_INVALID_DATA)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 0)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_NULL_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // hdr_0: addr=0xfe,inc=0 -> addr:b0=1, addr:b1=0
    _uart_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xfe, ifmap, 16, &transferred, 0, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 2)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 1)) {
      _uart_record_t *rec = memx_list_pop(_uart_write_records);
      if(memx_test_assert_not_null(rec)
        && memx_test_assert_equal(rec->length, 8)
        && memx_test_assert_equal(((uint8_t*)rec->data)[0], 0xff) // preamble
        && memx_test_assert_equal(((uint8_t*)rec->data)[1], 0xaa) // preamble
        && memx_test_assert_equal(((uint8_t*)rec->data)[2], 0x00) // preamble
        && memx_test_assert_equal(((uint8_t*)rec->data)[3], 0x03) // payload-word-length
        && memx_test_assert_equal(((uint8_t*)rec->data)[4], 0xfd) // address-and-op
        && memx_test_assert_equal(((uint8_t*)rec->data)[5], 0x00) // address
        && memx_test_assert_equal(((uint8_t*)rec->data)[6], 0x00) // address
        && memx_test_assert_equal(((uint8_t*)rec->data)[7], 0x00)) { // address
      } else {
        memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_HDR_0_FAIL);
      }
      _uart_record_destroy(rec);
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_HDR_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // hdr_1: addr=0xfc,inc=1 -> addr:b0=1, addr:b1=1
    _uart_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xfc, ifmap, 16, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 2)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 1)) {
      _uart_record_t *rec = memx_list_pop(_uart_write_records);
      if(memx_test_assert_not_null(rec)
        && memx_test_assert_equal(rec->length, 8)
        && memx_test_assert_equal(((uint8_t*)rec->data)[0], 0xff) // preamble
        && memx_test_assert_equal(((uint8_t*)rec->data)[1], 0xaa) // preamble
        && memx_test_assert_equal(((uint8_t*)rec->data)[2], 0x00) // preamble
        && memx_test_assert_equal(((uint8_t*)rec->data)[3], 0x03) // payload-word-length
        && memx_test_assert_equal(((uint8_t*)rec->data)[4], 0xff) // address-and-op
        && memx_test_assert_equal(((uint8_t*)rec->data)[5], 0x00) // address
        && memx_test_assert_equal(((uint8_t*)rec->data)[6], 0x00) // address
        && memx_test_assert_equal(((uint8_t*)rec->data)[7], 0x00)) { // address
      } else {
        memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_HDR_1_FAIL);
      }
      _uart_record_destroy(rec);
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_HDR_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: length=-1 -> report error
    _uart_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xfc, ifmap, -1, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_UART2AHB_INVALID_LENGTH)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 0)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_1: length=0 -> transferred=0
    _uart_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xfc, ifmap, 0, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 0)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_MIN_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: length=1 -> transferred=4
    _uart_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xfc, ifmap, 1, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 2)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 1)
      && memx_test_assert_equal(transferred, 4)) {
      _uart_record_t* w_rec_0 = memx_list_pop(_uart_write_records);
      _uart_record_t* w_rec_1 = memx_list_pop(_uart_write_records);
      if(memx_test_assert_not_null(w_rec_0)
        && memx_test_assert_equal(w_rec_0->length, 8)
        && memx_test_assert_not_null(w_rec_1)
        && memx_test_assert_equal(w_rec_1->length, 4)
        && memx_test_assert_equal(w_rec_1->data, ifmap+0)) {
      } else {
        memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_MID_0_FAIL);
      }
      _uart_record_destroy(w_rec_0);
      _uart_record_destroy(w_rec_1);
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_0: length=max -> transferred=max
    _uart_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xfc, ifmap, 512, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 2)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 1)
      && memx_test_assert_equal(transferred, 512)) {
      _uart_record_t* w_rec_0 = memx_list_pop(_uart_write_records);
      _uart_record_t* w_rec_1 = memx_list_pop(_uart_write_records);
      if(memx_test_assert_not_null(w_rec_0)
        && memx_test_assert_equal(w_rec_0->length, 8)
        && memx_test_assert_not_null(w_rec_1)
        && memx_test_assert_equal(w_rec_1->length, 512)
        && memx_test_assert_equal(w_rec_1->data, ifmap+0)) {
      } else {
        memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_MAX_0_FAIL);
      }
      _uart_record_destroy(w_rec_0);
      _uart_record_destroy(w_rec_1);
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_MAX_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_1: length=max+1 -> transferred=max+4
    _uart_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xfc, ifmap, 513, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 4)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 2)
      && memx_test_assert_equal(transferred, 512+4)) {
      _uart_record_t* w_rec_0 = memx_list_pop(_uart_write_records);
      _uart_record_t* w_rec_1 = memx_list_pop(_uart_write_records);
      _uart_record_t* w_rec_2 = memx_list_pop(_uart_write_records);
      _uart_record_t* w_rec_3 = memx_list_pop(_uart_write_records);
      if(memx_test_assert_not_null(w_rec_0) // header
        && memx_test_assert_equal(w_rec_0->length, 8)
        && memx_test_assert_not_null(w_rec_1) // payload
        && memx_test_assert_equal(w_rec_1->length, 512)
        && memx_test_assert_equal(w_rec_1->data, ifmap+0)
        && memx_test_assert_not_null(w_rec_2) // header
        && memx_test_assert_equal(w_rec_2->length, 8)
        && memx_test_assert_not_null(w_rec_3) // poyload
        && memx_test_assert_equal(w_rec_3->length, 4)
        && memx_test_assert_equal(w_rec_3->data, ifmap+512)) {
      } else {
        memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_MAX_1_FAIL);
      }
      _uart_record_destroy(w_rec_0);
      _uart_record_destroy(w_rec_1);
      _uart_record_destroy(w_rec_2);
      _uart_record_destroy(w_rec_3);
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_WRITE_MAX_1_FAIL);
    }
  }

  memx_mpuio_destroy(mpuio); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_uart2ahb_control_read)
{
  memx_status status = MEMX_STATUS_OK;
  uint8_t ofmap[1024] = {0};
  int transferred = 0;

  MemxMpuIo* mpuio = memx_uart2ahb_create("/dev/ttyUSB0", 921600, 100);
  if(memx_test_assert_not_null(mpuio)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: context=nullptr -> report error
    _uart_records_destroy_all();
    status = mpuio->control_read(NULL, 1, 0xff, ofmap, 16, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_UART2AHB_INVALID_CONTEXT)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 0)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // null_1: data=nullptr -> report
    _uart_records_destroy_all();
    status = memx_mpuio_control_read(mpuio, 1, 0xff, NULL, 16, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_UART2AHB_INVALID_DATA)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 0)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_NULL_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // hdr_0: addr=0xff,inc=0 -> addr:b0=0, addr:b1=0
    _uart_records_destroy_all();
    status = memx_mpuio_control_read(mpuio, 1, 0xff, ofmap, 16, &transferred, 0, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 1)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 2)) {
      _uart_record_t* w_rec_0 = memx_list_pop(_uart_write_records);
      if(memx_test_assert_not_null(w_rec_0)
        && memx_test_assert_equal(w_rec_0->length, 8)
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[0], 0xff) // preamble
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[1], 0xaa) // preamble
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[2], 0x00) // preamble
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[3], 0x03) // payload-word-length
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[4], 0xfc) // address-and-op
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[5], 0x00) // address
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[6], 0x00) // address
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[7], 0x00)) { // address
      } else {
        memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_HDR_0_FAIL);
      }
      _uart_record_destroy(w_rec_0);
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_HDR_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // hdr_1: addr=0xfd,inc=0 -> addr:b0=0, addr:b1=1
    _uart_records_destroy_all();
    status = memx_mpuio_control_read(mpuio, 1, 0xfd, ofmap, 16, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 1)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 2)) {
      _uart_record_t* w_rec_0 = memx_list_pop(_uart_write_records);
      if(memx_test_assert_not_null(w_rec_0)
        && memx_test_assert_equal(w_rec_0->length, 8)
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[0], 0xff) // preamble
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[1], 0xaa) // preamble
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[2], 0x00) // preamble
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[3], 0x03) // payload-word-length
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[4], 0xfe) // address-and-op
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[5], 0x00) // address
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[6], 0x00) // address
        && memx_test_assert_equal(((uint8_t*)w_rec_0->data)[7], 0x00)) { // address
      } else {
        memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_HDR_1_FAIL);
      }
      _uart_record_destroy(w_rec_0);
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_HDR_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: length=-1 -> report error
    _uart_records_destroy_all();
    status = memx_mpuio_control_read(mpuio, 1, 0xfc, ofmap, -1, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_UART2AHB_INVALID_LENGTH)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 0)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_1: length=0, transferred=0
    _uart_records_destroy_all();
    status = memx_mpuio_control_read(mpuio, 1, 0xfc, ofmap, 0, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 0)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_MIN_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: length=1, transferred=4
    _uart_records_destroy_all();
    status = memx_mpuio_control_read(mpuio, 1, 0xfc, ofmap, 1, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 1)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 2)
      && memx_test_assert_equal(transferred, 4)
      && memx_test_assert_equal(ofmap[0], 0)
      && memx_test_assert_equal(ofmap[3], 3)) {
      _uart_record_t* r_rec_0 = memx_list_pop(_uart_read_records);
      _uart_record_t* r_rec_1 = memx_list_pop(_uart_read_records);
      if(memx_test_assert_not_null(r_rec_0)
        && memx_test_assert_not_null(r_rec_1)
        && memx_test_assert_equal(r_rec_1->length, 4)
        && memx_test_assert_equal(r_rec_1->data, ofmap+0)) {
      } else {
        memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_MID_0_FAIL);
      }
      _uart_record_destroy(r_rec_0);
      _uart_record_destroy(r_rec_1);
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_0: length=max, transferred=max
    _uart_records_destroy_all();
    status = memx_mpuio_control_read(mpuio, 1, 0xfc, ofmap, 256, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 1)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 2)
      && memx_test_assert_equal(transferred, 256)
      && memx_test_assert_equal(ofmap[0], 0)
      && memx_test_assert_equal(ofmap[255], 255)) {
      _uart_record_t* r_rec_0 = memx_list_pop(_uart_read_records);
      _uart_record_t* r_rec_1 = memx_list_pop(_uart_read_records);
      if(memx_test_assert_not_null(r_rec_0)
        && memx_test_assert_not_null(r_rec_1)
        && memx_test_assert_equal(r_rec_1->length, 256)
        && memx_test_assert_equal(r_rec_1->data, ofmap+0)) {
      } else {
        memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_MAX_0_FAIL);
      }
      _uart_record_destroy(r_rec_0);
      _uart_record_destroy(r_rec_1);
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_MAX_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_1: length=max+1 -> transferred=max+4
    _uart_records_destroy_all();
    status = memx_mpuio_control_read(mpuio, 1, 0xfc, ofmap, 257, &transferred, 1, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_uart_write_records), 2)
      && memx_test_assert_equal(memx_list_count(_uart_read_records), 4)
      && memx_test_assert_equal(transferred, 260)
      && memx_test_assert_equal(ofmap[0], 0)
      && memx_test_assert_equal(ofmap[255], 255)
      && memx_test_assert_equal(ofmap[256], 0)
      && memx_test_assert_equal(ofmap[259], 3)) {
      _uart_record_t* r_rec_0 = memx_list_pop(_uart_read_records);
      _uart_record_t* r_rec_1 = memx_list_pop(_uart_read_records);
      _uart_record_t* r_rec_2 = memx_list_pop(_uart_read_records);
      _uart_record_t* r_rec_3 = memx_list_pop(_uart_read_records);
      if(memx_test_assert_not_null(r_rec_0)
        && memx_test_assert_not_null(r_rec_1)
        && memx_test_assert_equal(r_rec_1->length, 256)
        && memx_test_assert_equal(r_rec_1->data, ofmap+0)
        && memx_test_assert_not_null(r_rec_2)
        && memx_test_assert_not_null(r_rec_3)
        && memx_test_assert_equal(r_rec_3->length, 4)
        && memx_test_assert_equal(r_rec_3->data, ofmap+256)) {
      } else {
        memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_MAX_1_FAIL);
      }
      _uart_record_destroy(r_rec_0);
      _uart_record_destroy(r_rec_1);
      _uart_record_destroy(r_rec_2);
      _uart_record_destroy(r_rec_3);
    } else {
      memx_test_result_set(MEMX_TEST_UART2AHB_CONTROL_READ_MAX_1_FAIL);
    }
  }

  memx_mpuio_destroy(mpuio); // clean-up

} MEMX_TESTCASE_END;

/***************************************************************************//**
 * test sequence
 ******************************************************************************/
int main(int argc, char** argv)
{
  memx_test_result result = MEMX_TEST_PASS;
  _uart_write_records = memx_list_create();
  _uart_read_records = memx_list_create();

  if(memx_test_result_okay)
    result = test_memx_uart2ahb_create();
  if(memx_test_result_okay)
    result = test_memx_uart2ahb_control_write();
  if(memx_test_result_okay)
    result = test_memx_uart2ahb_control_read();

  _uart_records_destroy_all();
  memx_list_destroy(_uart_write_records);
  memx_list_destroy(_uart_read_records);
  unused(argc);
  unused(argv);
  return 0;
}

