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
#include "memx_ccif.h"

#define _CCIF_TX_TRANSFER_CHUNK_SIZE  (4096) // TX transfer chunk size, must align to 'memx_ccif.c'
#define _CCIF_RX_TRANSFER_CHUNK_SIZE  (4096) // RX transfer chunk size, must align to 'memx_ccif.c'

/***************************************************************************//**
 * interface stub function
 ******************************************************************************/
typedef struct _ccif_transfer_record_t
{
  uint8_t* source; // stores original data source pointer
  uint8_t* content; // needs to make a copy of data for header will be released right after function call is finished
  int length;
} _ccif_transfer_record_t;
static MemxList* _ccif_write_transfer_records = NULL;

#define _ccif_transfer_record_destroy(_rec_) \
  do { \
    if((_rec_) != NULL) { \
      free(_rec_->content); \
      free(_rec_); \
    } \
  } while(0)
#define _ccif_transfer_records_destroy_all() \
  do { \
    _ccif_transfer_record_t *_rec_ = NULL; \
    while((_rec_ = memx_list_pop(_ccif_write_transfer_records)) != NULL) { \
      free(_rec_->content); \
      free(_rec_); \
    } \
  } while(0)

static MemxList* _usb_on_going_bulk_out_transfers = NULL;
extern int _usb_create_context(MemxCcif* context)
{
  unused(context);
  return LIBUSB_SUCCESS;
}
extern int _usb_destroy_context(MemxCcif* context)
{
  unused(context);
  return LIBUSB_SUCCESS;
}
extern int _usb_open_device(MemxCcif* context, MemxCcifUsbDevice* usb_device, uint16_t vendor_id, uint8_t product_id, uint8_t ep_out, uint8_t ep_in, int interface)
{
  usb_device->ep_out = ep_out;
  usb_device->ep_in = ep_in;
  unused(context);
  unused(vendor_id);
  unused(product_id);
  unused(interface);
  return LIBUSB_SUCCESS;
}
extern int _usb_close_device(MemxCcif* context, MemxCcifUsbDevice* usb_device)
{
  unused(context);
  unused(usb_device);
  return LIBUSB_SUCCESS;
}
extern int _usb_reset_device(MemxCcif* context, MemxCcifUsbDevice* usb_device)
{
  unused(context);
  unused(usb_device);
  return LIBUSB_SUCCESS;
}
extern int _usb_claim_device(MemxCcif* context, MemxCcifUsbDevice* usb_device)
{
  unused(context);
  unused(usb_device);
  return LIBUSB_SUCCESS;
}
extern int _usb_release_device(MemxCcif* context, MemxCcifUsbDevice* usb_device)
{
  unused(context);
  unused(usb_device);
  return LIBUSB_SUCCESS;
}
extern int _usb_listen_to_on_the_air_events(MemxCcif* context)
{
  struct libusb_transfer* transfer = NULL;
  MemxCcifUsbTransfer* usb_transfer = NULL;
  while ((transfer = memx_list_pop(_usb_on_going_bulk_out_transfers)) != NULL) {
    transfer->actual_length = transfer->length;
    transfer->status = LIBUSB_SUCCESS;
    usb_transfer = (MemxCcifUsbTransfer*)(transfer->user_data);
    ((MemxCcifUsbTransferUserCallback)(usb_transfer->callback_data.user_callback))(usb_transfer->callback_data.user_data);
  }
  unused(context);
  return LIBUSB_SUCCESS;
}
extern MemxCcifUsbTransfer* _usb_allocate_transfer()
{
  MemxCcifUsbTransfer* usb_transfer = (MemxCcifUsbTransfer*)malloc(sizeof(MemxCcifUsbTransfer));
  if(usb_transfer != NULL) {
    usb_transfer->transfer = libusb_alloc_transfer(0);
    usb_transfer->callback_data.user_callback = NULL;
    usb_transfer->callback_data.user_data = NULL;
    if(usb_transfer->transfer != NULL)
      return usb_transfer;
    free(usb_transfer);
  }
  return NULL;
}
extern int _usb_free_transfer(MemxCcifUsbTransfer* usb_transfer)
{
  if(usb_transfer != NULL) {
    libusb_free_transfer(usb_transfer->transfer);
    free(usb_transfer);
  }
  return LIBUSB_SUCCESS;
}
extern int _usb_fill_transfer_bulk_out(MemxCcifUsbTransfer* usb_transfer, MemxCcifUsbDevice* usb_device, uint8_t* data, int length, int timeout, void* user_callback, void* user_data)
{
  struct libusb_transfer* transfer = NULL;
  if(usb_transfer != NULL) {
    transfer = usb_transfer->transfer;
    transfer->endpoint = usb_device->ep_out;
    transfer->buffer = data;
    transfer->length = length;
    usb_transfer->callback_data.user_callback = user_callback;
    usb_transfer->callback_data.user_data = user_data;
    transfer->user_data = usb_transfer;
  }
  unused(timeout);
  return LIBUSB_SUCCESS;
}
extern int _usb_fill_transfer_bulk_in(MemxCcifUsbTransfer* usb_transfer, MemxCcifUsbDevice* usb_device, uint8_t* data, int length, int timeout, void* user_callback, void* user_data)
{
  struct libusb_transfer* transfer = NULL;
  if(usb_transfer != NULL) {
    transfer = usb_transfer->transfer;
    transfer->endpoint = usb_device->ep_in;
    transfer->buffer = data;
    transfer->length = length;
    usb_transfer->callback_data.user_callback = user_callback;
    usb_transfer->callback_data.user_data = user_data;
    transfer->user_data = usb_transfer;
  }
  unused(timeout);
  return LIBUSB_SUCCESS;
}
extern int _usb_submit_transfer(MemxCcifUsbTransfer* usb_transfer)
{
  if(usb_transfer == NULL)
    return LIBUSB_ERROR_OTHER;
  if(usb_transfer->transfer->endpoint & 0x80) {
    // since there are infinite polling bulk-in requests during runtime
    // just ignore the bulk-in request and heck into received data directly
  } else {
    if(memx_list_push(_usb_on_going_bulk_out_transfers, usb_transfer->transfer))
      return LIBUSB_ERROR_OTHER;
    _ccif_transfer_record_t* rec = (_ccif_transfer_record_t*)malloc(sizeof(_ccif_transfer_record_t));
    rec->source = usb_transfer->transfer->buffer;
    rec->length = usb_transfer->transfer->length;
    rec->content = (uint8_t*)malloc((rec->length)*sizeof(uint8_t));
    memcpy(rec->content, rec->source, rec->length); // copy or will be released later
    memx_list_push(_ccif_write_transfer_records, rec);
  }
  return LIBUSB_SUCCESS;
}
extern int _usb_parse_transfer_result(MemxCcifUsbTransfer* usb_transfer, uint8_t** data, int* transferred)
{
  struct libusb_transfer* transfer;
  if(usb_transfer != NULL) {
    transfer = usb_transfer->transfer;
    *data = transfer->buffer;
    *transferred = transfer->actual_length;
    return transfer->status;
  }
  return LIBUSB_ERROR_OTHER;
}
extern int _usb_sync_transfer_bulk_out(MemxCcifUsbDevice* usb_device, uint8_t* data, int length, int* transferred, int timeout)
{
  _ccif_transfer_record_t* rec = (_ccif_transfer_record_t*)malloc(sizeof(_ccif_transfer_record_t));
  rec->source = data;
  rec->length = length;
  rec->content = (uint8_t*)malloc((rec->length)*sizeof(uint8_t));
  memcpy(rec->content, rec->source, rec->length); // copy or will be released later
  memx_list_push(_ccif_write_transfer_records, rec);

  // update transferred length here directly
  *transferred = length;

  unused(usb_device);
  unused(timeout);
  return LIBUSB_SUCCESS;
}
extern int _usb_sync_transfer_bulk_in(MemxCcifUsbDevice* usb_device, uint8_t* data, int length, int* transferred, int timeout)
{
  unused(usb_device);
  unused(data);
  unused(length);
  unused(transferred);
  unused(timeout);
  return LIBUSB_SUCCESS;
}

typedef struct
{
  MemxMpuIo* mpuio;
  uint8_t* data;
  int length;
  int is_stop_set;
} _HeckSharedRingbufferWorkerData;
static void* _ccif_shared_ringbiffer_inject(void* user_data)
{
  _HeckSharedRingbufferWorkerData* worker_data = (_HeckSharedRingbufferWorkerData*)user_data;
  MemxCcif* context = (MemxCcif*)(worker_data->mpuio->context);

  context->shared_ringbuffer->ptr_read = context->shared_ringbuffer->ptr_write;
  for(int i=0; (i<worker_data->length)&&(worker_data->is_stop_set == 0);)
    i += memx_ringbuffer_put(context->shared_ringbuffer, worker_data->data+i, worker_data->length-i);
  return NULL;
}

#define _ccif_shared_ringbuffer_inject_start(_worker_, _worker_data_, _mpuio_, _header_, _data_, _length_) \
  do { \
    _data_[0] = _header_; \
    _worker_data_.mpuio = _mpuio_; \
    _worker_data_.data = (uint8_t*)_data_; \
    _worker_data_.length = _length_; \
    _worker_data_.is_stop_set = 0; \
    pthread_create(&_worker_, NULL, &_ccif_shared_ringbiffer_inject, &_worker_data_); \
  } while(0)
#define _ccif_shared_ringbuffer_inject_stop(_worker_, _worker_data_) \
  do { \
    _worker_data_.is_stop_set = 1; \
    pthread_join(_worker_, NULL); \
  } while(0)

/***************************************************************************//**
 * testcase
 ******************************************************************************/
MEMX_TESTCASE(test_memx_ccif_create)
{
  if(memx_test_result_okay) { // mid_0: device='cypress fx3' -> return nullptr
    MemxMpuIo* mpuio = memx_ccif_create(0x4b4, 0xf4, 0x01, 0, 0x4b4, 0xf2, 0x81, 0);
    if(memx_test_assert_not_null(mpuio)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CREATE_MID_0_FAIL);
    }
    memx_mpuio_destroy(mpuio);
  }
} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_ccif_control_write)
{
  memx_status status = MEMX_STATUS_OK;
  uint8_t ifmap[_CCIF_TX_TRANSFER_CHUNK_SIZE+4] = {0};
  int transferred = 0;

  MemxMpuIo* mpuio = memx_ccif_create(0x4b4, 0xf4, 0x01, 0, 0x4b4, 0xf2, 0x81, 0);
  if(memx_test_assert_not_null(mpuio)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_CCIF_CONTROL_WRITE_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: context=nullptr -> report error
    _ccif_transfer_records_destroy_all();
    status = mpuio->control_write(NULL, 1, 0xff, ifmap, 16, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_INVALID_CONTEXT)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_WRITE_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // null_1: data=nullptr -> report error
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xff, NULL, 16, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_INVALID_DATA)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_WRITE_NULL_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // hdr_0: length=16 -> peek header
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0x12345678, ifmap, 16, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 2)) {
      _ccif_transfer_record_t* w_rec_0 = memx_list_pop(_ccif_write_transfer_records);
      if(memx_test_assert_not_null(w_rec_0)
        && memx_test_assert_equal(w_rec_0->length, 8)
        && memx_test_assert_equal(((uint32_t*)w_rec_0->content)[0], 0x20040004) // command
        && memx_test_assert_equal(((uint32_t*)w_rec_0->content)[1], 0x12345678)) { // address
      } else {
        memx_test_result_set(MEMX_TEST_CCIF_CONTROL_WRITE_HDR_0_FAIL);
      }
      _ccif_transfer_record_destroy(w_rec_0);
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_WRITE_HDR_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: length=-1 -> report error
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xfc, ifmap, -1, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_INVALID_LENGTH)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_WRITE_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_1: length=0 -> transferred=0
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xfc, ifmap, 0, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_WRITE_MIN_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: length=1 -> transferred=4
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xfc, ifmap, 1, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 2)
      && memx_test_assert_equal(transferred, 4)) {
      _ccif_transfer_record_t* w_rec_0 = memx_list_pop(_ccif_write_transfer_records);
      _ccif_transfer_record_t* w_rec_1 = memx_list_pop(_ccif_write_transfer_records);
      if(memx_test_assert_not_null(w_rec_0)
        && memx_test_assert_equal(w_rec_0->length, 8)
        && memx_test_assert_not_null(w_rec_1)
        && memx_test_assert_equal(w_rec_1->length, 4)
        && memx_test_assert_equal(w_rec_1->source, ifmap+0)) {
      } else {
        memx_test_result_set(MEMX_TEST_CCIF_CONTROL_WRITE_MID_0_FAIL);
      }
      _ccif_transfer_record_destroy(w_rec_0);
      _ccif_transfer_record_destroy(w_rec_1);
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_WRITE_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_0: length=max -> transferred=max
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xfc, ifmap, _CCIF_TX_TRANSFER_CHUNK_SIZE, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 2)
      && memx_test_assert_equal(transferred, _CCIF_TX_TRANSFER_CHUNK_SIZE)) {
      _ccif_transfer_record_t* w_rec_0 = memx_list_pop(_ccif_write_transfer_records);
      _ccif_transfer_record_t* w_rec_1 = memx_list_pop(_ccif_write_transfer_records);
      if(memx_test_assert_not_null(w_rec_0)
        && memx_test_assert_equal(w_rec_0->length, 8)
        && memx_test_assert_not_null(w_rec_1)
        && memx_test_assert_equal(w_rec_1->length, _CCIF_TX_TRANSFER_CHUNK_SIZE)
        && memx_test_assert_equal(w_rec_1->source, ifmap+0)) {
      } else {
        memx_test_result_set(MEMX_TEST_CCIF_CONTROL_WRITE_MAX_0_FAIL);
      }
      _ccif_transfer_record_destroy(w_rec_0);
      _ccif_transfer_record_destroy(w_rec_1);
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_WRITE_MAX_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_1: length=max+1 -> transferred=max+4
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_control_write(mpuio, 1, 0xfc, ifmap, _CCIF_TX_TRANSFER_CHUNK_SIZE+1, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 4)
      && memx_test_assert_equal(transferred, _CCIF_TX_TRANSFER_CHUNK_SIZE+4)) {
      _ccif_transfer_record_t* w_rec_0 = memx_list_pop(_ccif_write_transfer_records);
      _ccif_transfer_record_t* w_rec_1 = memx_list_pop(_ccif_write_transfer_records);
      _ccif_transfer_record_t* w_rec_2 = memx_list_pop(_ccif_write_transfer_records);
      _ccif_transfer_record_t* w_rec_3 = memx_list_pop(_ccif_write_transfer_records);
      if(memx_test_assert_not_null(w_rec_0) // header
        && memx_test_assert_equal(w_rec_0->length, 8)
        && memx_test_assert_not_null(w_rec_1) // payload
        && memx_test_assert_equal(w_rec_1->length, _CCIF_TX_TRANSFER_CHUNK_SIZE)
        && memx_test_assert_equal(w_rec_1->source, ifmap+0)
        && memx_test_assert_not_null(w_rec_2) // header
        && memx_test_assert_equal(w_rec_2->length, 8)
        && memx_test_assert_not_null(w_rec_3) // payload
        && memx_test_assert_equal(w_rec_3->length, 4)
        && memx_test_assert_equal(w_rec_3->source, ifmap+_CCIF_TX_TRANSFER_CHUNK_SIZE)) {
      } else {
        memx_test_result_set(MEMX_TEST_CCIF_CONTROL_WRITE_MAX_1_FAIL);
      }
      _ccif_transfer_record_destroy(w_rec_0);
      _ccif_transfer_record_destroy(w_rec_1);
      _ccif_transfer_record_destroy(w_rec_2);
      _ccif_transfer_record_destroy(w_rec_3);
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_WRITE_MAX_1_FAIL);
    }
  }

  memx_mpuio_destroy(mpuio); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_ccif_control_read)
{
  memx_status status = MEMX_STATUS_OK;
  uint32_t ifmap[2+(_CCIF_RX_TRANSFER_CHUNK_SIZE>>2)+1] = {0x40040004, 0x55aa55aa, 0xffeeddcc, 0xbbaa9988, 0x77665544, 0x33221100};
  uint8_t ofmap[_CCIF_RX_TRANSFER_CHUNK_SIZE+4] = {0};
  int transferred = 0;
  ifmap[2+(_CCIF_RX_TRANSFER_CHUNK_SIZE>>2)-1] = 0x55aa55aa;
  ifmap[2+(_CCIF_RX_TRANSFER_CHUNK_SIZE>>2)-0] = 0xaa55aa55;

  _HeckSharedRingbufferWorkerData worker_data;
  pthread_t worker;

  MemxMpuIo* mpuio = memx_ccif_create(0x4b4, 0xf4, 0x01, 0, 0x4b4, 0xf2, 0x81, 0);
  if(memx_test_assert_not_null(mpuio)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_CCIF_CONTROL_READ_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: context=nullptr -> report error
    _ccif_transfer_records_destroy_all();
    status = mpuio->control_read(NULL, 1, 0xff, ofmap, 16, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_INVALID_CONTEXT)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_READ_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // null_1: data=nullptr -> report
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_control_read(mpuio, 1, 0xff, NULL, 16, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_INVALID_DATA)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_READ_NULL_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // hdr_0: length=16 -> peek header
    _ccif_transfer_records_destroy_all();
    _ccif_shared_ringbuffer_inject_start(worker, worker_data, mpuio, 0x40040004, ifmap, 8+16);
    status = memx_mpuio_control_read(mpuio, 1, 0x12345678, ofmap, 16, &transferred, 0, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 1+1)) {
      _ccif_transfer_record_t* w_rec_0 = memx_list_pop(_ccif_write_transfer_records);
      if(memx_test_assert_not_null(w_rec_0)
        && memx_test_assert_equal(w_rec_0->length, 8)
        && memx_test_assert_equal(((uint32_t*)w_rec_0->content)[0], 0x30040004) // command
        && memx_test_assert_equal(((uint32_t*)w_rec_0->content)[1], 0x12345678)) { // address
      } else {
        memx_test_result_set(MEMX_TEST_CCIF_CONTROL_READ_HDR_0_FAIL);
      }
      _ccif_transfer_record_destroy(w_rec_0);
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_READ_HDR_0_FAIL);
    }
    _ccif_shared_ringbuffer_inject_stop(worker, worker_data);
  }
  if(memx_test_result_okay) { // min_0: length=-1 -> report error
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_control_read(mpuio, 1, 0xfc, ofmap, -1, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_INVALID_LENGTH)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_READ_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_1: length=0, transferred=0
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_control_read(mpuio, 1, 0xfc, ofmap, 0, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)
      && memx_test_assert_equal(transferred, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_READ_MIN_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: length=1, transferred=4
    _ccif_transfer_records_destroy_all();
    _ccif_shared_ringbuffer_inject_start(worker, worker_data, mpuio, 0x40040001, ifmap, 8+4);
    status = memx_mpuio_control_read(mpuio, 1, 0xfc, ofmap, 1, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 1+1)
      && memx_test_assert_equal(transferred, 4)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[0], 0xffeeddcc)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_READ_MID_0_FAIL);
    }
    _ccif_shared_ringbuffer_inject_stop(worker, worker_data);
  }
  if(memx_test_result_okay) { // max_0: length=max, transferred=max
    _ccif_transfer_records_destroy_all();
    _ccif_shared_ringbuffer_inject_start(worker, worker_data, mpuio, 0x40040000|(_CCIF_RX_TRANSFER_CHUNK_SIZE>>2), ifmap, 8+_CCIF_RX_TRANSFER_CHUNK_SIZE);
    status = memx_mpuio_control_read(mpuio, 1, 0xfc, ofmap, _CCIF_RX_TRANSFER_CHUNK_SIZE, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 1+1)
      && memx_test_assert_equal(transferred, _CCIF_RX_TRANSFER_CHUNK_SIZE)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[0], 0xffeeddcc)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[12], 0x33221100)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[_CCIF_RX_TRANSFER_CHUNK_SIZE-4], 0x55aa55aa)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_READ_MAX_0_FAIL);
    }
    _ccif_shared_ringbuffer_inject_stop(worker, worker_data);
  }
  if(memx_test_result_okay) { // max_1: length=max+1 -> transferred=max+4
    _ccif_transfer_records_destroy_all();
    _ccif_shared_ringbuffer_inject_start(worker, worker_data, mpuio, 0x40040000|((_CCIF_RX_TRANSFER_CHUNK_SIZE>>2)+1), ifmap, 8+_CCIF_RX_TRANSFER_CHUNK_SIZE+4);
    status = memx_mpuio_control_read(mpuio, 1, 0xfc, ofmap, _CCIF_RX_TRANSFER_CHUNK_SIZE+1, &transferred, 1, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 2+1)
      && memx_test_assert_equal(transferred, _CCIF_RX_TRANSFER_CHUNK_SIZE+4)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[0], 0xffeeddcc)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[12], 0x33221100)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[_CCIF_RX_TRANSFER_CHUNK_SIZE-4], 0x55aa55aa)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[_CCIF_RX_TRANSFER_CHUNK_SIZE+0], 0xaa55aa55)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_READ_MAX_1_FAIL);
    }
    _ccif_shared_ringbuffer_inject_stop(worker, worker_data);
  }
  if(memx_test_result_okay) { // timeout_0: length=0 -> transferred=0, report timeout
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_control_read(mpuio, 1, 0xfc, ofmap, 4, &transferred, 1, 10);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_WAIT_TRANSFER_TIMEOUT)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 1+1)
      && memx_test_assert_equal(transferred, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_CONTROL_READ_TIMEOUT_0_FAIL);
    }
  }

  memx_mpuio_destroy(mpuio); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_ccif_stream_write)
{
  memx_status status = MEMX_STATUS_OK;
  uint8_t ifmap[_CCIF_TX_TRANSFER_CHUNK_SIZE+4] = {0};
  int transferred = 0;

  MemxMpuIo* mpuio = memx_ccif_create(0x4b4, 0xf4, 0x01, 0, 0x4b4, 0xf2, 0x81, 0);
  if(memx_test_assert_not_null(mpuio)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_CCIF_STREAM_WRITE_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: context=nullptr -> report error
    _ccif_transfer_records_destroy_all();
    status = mpuio->stream_write(NULL, 1, 1, ifmap, 16, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_INVALID_CONTEXT)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_WRITE_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // null_1: data=nullptr -> report error
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_stream_write(mpuio, 1, 1, NULL, 16, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_INVALID_DATA)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_WRITE_NULL_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // hdr_0: length=16 -> peek header
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_stream_write(mpuio, 1, 1, ifmap, 16, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 2)) {
      _ccif_transfer_record_t* w_rec_0 = memx_list_pop(_ccif_write_transfer_records);
      if(memx_test_assert_not_null(w_rec_0)
        && memx_test_assert_equal(w_rec_0->length, 4)
        && memx_test_assert_equal(((uint32_t*)w_rec_0->content)[0], 0x51040003)) {
      } else {
        memx_test_result_set(MEMX_TEST_CCIF_STREAM_WRITE_HDR_0_FAIL);
      }
      _ccif_transfer_record_destroy(w_rec_0);
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_WRITE_HDR_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: length=-1 -> report error
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_stream_write(mpuio, 1, 1, ifmap, -1, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_INVALID_LENGTH)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_WRITE_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_1: length=0 -> transferred=0
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_stream_write(mpuio, 1, 1, ifmap, 0, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_WRITE_MIN_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: length=1 -> transferred=4
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_stream_write(mpuio, 1, 1, ifmap, 1, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 2)
      && memx_test_assert_equal(transferred, 4)) {
      _ccif_transfer_record_t* w_rec_0 = memx_list_pop(_ccif_write_transfer_records);
      _ccif_transfer_record_t* w_rec_1 = memx_list_pop(_ccif_write_transfer_records);
      if(memx_test_assert_not_null(w_rec_0)
        && memx_test_assert_equal(w_rec_0->length, 4)
        && memx_test_assert_not_null(w_rec_1)
        && memx_test_assert_equal(w_rec_1->length, 4)
        && memx_test_assert_equal(w_rec_1->source, ifmap+0)) {
      } else {
        memx_test_result_set(MEMX_TEST_CCIF_STREAM_WRITE_MID_0_FAIL);
      }
      _ccif_transfer_record_destroy(w_rec_0);
      _ccif_transfer_record_destroy(w_rec_1);
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_WRITE_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_0: length=max -> transferred=max
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_stream_write(mpuio, 1, 1, ifmap, _CCIF_TX_TRANSFER_CHUNK_SIZE, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 2)
      && memx_test_assert_equal(transferred, _CCIF_TX_TRANSFER_CHUNK_SIZE)) {
      _ccif_transfer_record_t* w_rec_0 = memx_list_pop(_ccif_write_transfer_records);
      _ccif_transfer_record_t* w_rec_1 = memx_list_pop(_ccif_write_transfer_records);
      if(memx_test_assert_not_null(w_rec_0)
        && memx_test_assert_equal(w_rec_0->length, 4)
        && memx_test_assert_not_null(w_rec_1)
        && memx_test_assert_equal(w_rec_1->length, _CCIF_TX_TRANSFER_CHUNK_SIZE)
        && memx_test_assert_equal(w_rec_1->source, ifmap+0)) {
      } else {
        memx_test_result_set(MEMX_TEST_CCIF_STREAM_WRITE_MAX_0_FAIL);
      }
      _ccif_transfer_record_destroy(w_rec_0);
      _ccif_transfer_record_destroy(w_rec_1);
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_WRITE_MAX_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_1: length=max+1 -> transferred=max+4
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_stream_write(mpuio, 1, 1, ifmap, _CCIF_TX_TRANSFER_CHUNK_SIZE+1, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 4)
      && memx_test_assert_equal(transferred, _CCIF_TX_TRANSFER_CHUNK_SIZE+4)) {
      _ccif_transfer_record_t* w_rec_0 = memx_list_pop(_ccif_write_transfer_records);
      _ccif_transfer_record_t* w_rec_1 = memx_list_pop(_ccif_write_transfer_records);
      _ccif_transfer_record_t* w_rec_2 = memx_list_pop(_ccif_write_transfer_records);
      _ccif_transfer_record_t* w_rec_3 = memx_list_pop(_ccif_write_transfer_records);
      if(memx_test_assert_not_null(w_rec_0) // header
        && memx_test_assert_equal(w_rec_0->length, 4)
        && memx_test_assert_not_null(w_rec_1) // payload
        && memx_test_assert_equal(w_rec_1->length, _CCIF_TX_TRANSFER_CHUNK_SIZE)
        && memx_test_assert_equal(w_rec_1->source, ifmap+0)
        && memx_test_assert_not_null(w_rec_2) // header
        && memx_test_assert_equal(w_rec_2->length, 4)
        && memx_test_assert_not_null(w_rec_3) // payload
        && memx_test_assert_equal(w_rec_3->length, 4)
        && memx_test_assert_equal(w_rec_3->source, ifmap+_CCIF_TX_TRANSFER_CHUNK_SIZE)) {
      } else {
        memx_test_result_set(MEMX_TEST_CCIF_STREAM_WRITE_MAX_1_FAIL);
      }
      _ccif_transfer_record_destroy(w_rec_0);
      _ccif_transfer_record_destroy(w_rec_1);
      _ccif_transfer_record_destroy(w_rec_2);
      _ccif_transfer_record_destroy(w_rec_3);
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_WRITE_MAX_1_FAIL);
    }
  }

  memx_mpuio_destroy(mpuio); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_ccif_stream_read)
{
  memx_status status = MEMX_STATUS_OK;
  uint32_t ifmap[1+(_CCIF_RX_TRANSFER_CHUNK_SIZE>>2)+1] = {0x51040000, 0xffeeddcc, 0xbbaa9988, 0x77665544, 0x33221100};
  uint8_t ofmap[_CCIF_RX_TRANSFER_CHUNK_SIZE+4] = {0};
  int transferred = 0;
  ifmap[1+(_CCIF_RX_TRANSFER_CHUNK_SIZE>>2)-1] = 0x55aa55aa;
  ifmap[1+(_CCIF_RX_TRANSFER_CHUNK_SIZE>>2)-0] = 0xaa55aa55;

  _HeckSharedRingbufferWorkerData worker_data;
  pthread_t worker;

  MemxMpuIo* mpuio = memx_ccif_create(0x4b4, 0xf4, 0x01, 0, 0x4b4, 0xf2, 0x81, 0);
  if(memx_test_assert_not_null(mpuio)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_CCIF_STREAM_READ_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: context=nullptr -> report error
    _ccif_transfer_records_destroy_all();
    status = mpuio->stream_read(NULL, 1, 1, ofmap, 16, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_INVALID_CONTEXT)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_READ_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // null_1: data=nullptr -> report error
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_stream_read(mpuio, 1, 1, NULL, 16, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_INVALID_DATA)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_READ_NULL_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // null_2: flow_id=15 -> report error
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_stream_read(mpuio, 1, 15, ofmap, 4, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_INVALID_FLOW_ID)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_READ_NULL_2_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: length=-1 -> report error
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_stream_read(mpuio, 1, 1, ofmap, -1, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_INVALID_LENGTH)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_READ_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_1: length=0, transferred=0
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_stream_read(mpuio, 1, 1, ofmap, 0, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)
      && memx_test_assert_equal(transferred, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_READ_MIN_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: length=1, transferred=4
    _ccif_transfer_records_destroy_all();
    _ccif_shared_ringbuffer_inject_start(worker, worker_data, mpuio, 0x51040000, ifmap, 4+4);
    status = memx_mpuio_stream_read(mpuio, 1, 1, ofmap, 1, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)
      && memx_test_assert_equal(transferred, 4)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[0], 0xffeeddcc)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_READ_MID_0_FAIL);
    }
    _ccif_shared_ringbuffer_inject_stop(worker, worker_data);
  }
  if(memx_test_result_okay) { // max_0: length=max, transferred=max
    _ccif_transfer_records_destroy_all();
    _ccif_shared_ringbuffer_inject_start(worker, worker_data, mpuio, 0x51040000|((_CCIF_RX_TRANSFER_CHUNK_SIZE>>2)-1), ifmap, 4+_CCIF_RX_TRANSFER_CHUNK_SIZE);
    status = memx_mpuio_stream_read(mpuio, 1, 1, ofmap, _CCIF_RX_TRANSFER_CHUNK_SIZE, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)
      && memx_test_assert_equal(transferred, _CCIF_RX_TRANSFER_CHUNK_SIZE)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[0], 0xffeeddcc)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[12], 0x33221100)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[_CCIF_RX_TRANSFER_CHUNK_SIZE-4], 0x55aa55aa)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_READ_MAX_0_FAIL);
    }
    _ccif_shared_ringbuffer_inject_stop(worker, worker_data);
  }
  if(memx_test_result_okay) { // max_1: length=max+1 -> transferred=max+4
    _ccif_transfer_records_destroy_all();
    _ccif_shared_ringbuffer_inject_start(worker, worker_data, mpuio, 0x51040000|(_CCIF_RX_TRANSFER_CHUNK_SIZE>>2), ifmap, 4+_CCIF_RX_TRANSFER_CHUNK_SIZE+4);
    status = memx_mpuio_stream_read(mpuio, 1, 1, ofmap, _CCIF_RX_TRANSFER_CHUNK_SIZE+1, &transferred, 100);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)
      && memx_test_assert_equal(transferred, _CCIF_RX_TRANSFER_CHUNK_SIZE+4)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[0], 0xffeeddcc)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[12], 0x33221100)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[_CCIF_RX_TRANSFER_CHUNK_SIZE-4], 0x55aa55aa)
      && memx_test_assert_equal(*(uint32_t*)&ofmap[_CCIF_RX_TRANSFER_CHUNK_SIZE+0], 0xaa55aa55)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_READ_MAX_1_FAIL);
    }
    _ccif_shared_ringbuffer_inject_stop(worker, worker_data);
  }
  if(memx_test_result_okay) { // timeout_0: length=0 -> transferred=0, report timeout
    _ccif_transfer_records_destroy_all();
    status = memx_mpuio_stream_read(mpuio, 1, 1, ofmap, 4, &transferred, 10);
    if(memx_test_assert_equal(status, MEMX_STATUS_CCIF_WAIT_TRANSFER_TIMEOUT)
      && memx_test_assert_equal(memx_list_count(_ccif_write_transfer_records), 0)
      && memx_test_assert_equal(transferred, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_CCIF_STREAM_READ_TIMEOUT_0_FAIL);
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
  _usb_on_going_bulk_out_transfers = memx_list_create();
  _ccif_write_transfer_records = memx_list_create();

  if(memx_test_result_okay)
    result = test_memx_ccif_create();
  if(memx_test_result_okay)
    result = test_memx_ccif_control_write();
  if(memx_test_result_okay)
    result = test_memx_ccif_control_read();
  if(memx_test_result_okay)
    result = test_memx_ccif_stream_write();
  if(memx_test_result_okay)
    result = test_memx_ccif_stream_read();

  _ccif_transfer_records_destroy_all();
  memx_list_destroy(_ccif_write_transfer_records);
  memx_list_destroy(_usb_on_going_bulk_out_transfers);
  unused(argc);
  unused(argv);
  return 0;
}

