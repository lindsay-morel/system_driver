/***************************************************************************//**
 * @note
 * Copyright (C) 2019-2022 MemryX Limited. All rights reserved.
 *
 ******************************************************************************/
#include "memx_common.h"
#include "memx_status.h"

#include "memx_test.h"
#include "memx_gbf.h"

/***************************************************************************//**
 * testcase
 ******************************************************************************/
MEMX_TESTCASE(test_memx_gbf_encode_float32_to_gbf80)
{
  memx_status status = MEMX_STATUS_OK;
  uint8_t gbf80_golden[20] = {0x19,0x3d,0x4e,0x5f,0x2f,0x82,0xc8,0x5a,0x90,0x81,0x49,0x06,0x4c,0x5c,0x0c,0x00,0x00,0x00,0x00,0x81};
  uint8_t gbf80_buffer[20] = {0};
  float flt32_golden[12] = {-0.781250,-0.937500,-6.593750,-7.343750,1.062500,2.125000,-3.343750,-1.000000,2.281250,0.093750,-0.593750,-4.343750};

  if(memx_test_result_okay) { // null_0: flt32=nullptr -> report error
    status = memx_gbf_encode_float32_to_gbf80(NULL, gbf80_buffer, 1);
    if(memx_test_assert_equal(status, MEMX_STATUS_GBF_INVALID_DATA)) {
    } else {
      memx_test_result_set(MEMX_TEST_GBF_ENCODE_FLOAT32_TO_GBF80_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // null_1: gbf80=nullptr -> report error
    status = memx_gbf_encode_float32_to_gbf80(flt32_golden, NULL, 1);
    if(memx_test_assert_equal(status, MEMX_STATUS_GBF_INVALID_DATA)) {
    } else {
      memx_test_result_set(MEMX_TEST_GBF_ENCODE_FLOAT32_TO_GBF80_NULL_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: length=0 -> do nothing
    status = memx_gbf_encode_float32_to_gbf80(flt32_golden, gbf80_buffer, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(gbf80_buffer[0], 0)
      && memx_test_assert_equal(gbf80_buffer[19], 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_GBF_ENCODE_FLOAT32_TO_GBF80_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: length=12 -> golden
    status = memx_gbf_encode_float32_to_gbf80(flt32_golden, gbf80_buffer, 12);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(gbf80_buffer[0], gbf80_golden[0])
      && memx_test_assert_equal(gbf80_buffer[1], gbf80_golden[1])
      && memx_test_assert_equal(gbf80_buffer[2], gbf80_golden[2])
      && memx_test_assert_equal(gbf80_buffer[3], gbf80_golden[3])
      && memx_test_assert_equal(gbf80_buffer[4], gbf80_golden[4])
      && memx_test_assert_equal(gbf80_buffer[5], gbf80_golden[5])
      && memx_test_assert_equal(gbf80_buffer[6], gbf80_golden[6])
      && memx_test_assert_equal(gbf80_buffer[7], gbf80_golden[7])
      && memx_test_assert_equal(gbf80_buffer[8], gbf80_golden[8])
      && memx_test_assert_equal(gbf80_buffer[9], gbf80_golden[9])
      && memx_test_assert_equal(gbf80_buffer[10], gbf80_golden[10])
      && memx_test_assert_equal(gbf80_buffer[11], gbf80_golden[11])
      && memx_test_assert_equal(gbf80_buffer[12], gbf80_golden[12])
      && memx_test_assert_equal(gbf80_buffer[13], gbf80_golden[13])
      && memx_test_assert_equal(gbf80_buffer[14], gbf80_golden[14])
      && memx_test_assert_equal(gbf80_buffer[15], gbf80_golden[15])
      && memx_test_assert_equal(gbf80_buffer[16], gbf80_golden[16])
      && memx_test_assert_equal(gbf80_buffer[17], gbf80_golden[17])
      && memx_test_assert_equal(gbf80_buffer[18], gbf80_golden[18])
      && memx_test_assert_equal(gbf80_buffer[19], gbf80_golden[19])) {
    } else {
      memx_test_result_set(MEMX_TEST_GBF_ENCODE_FLOAT32_TO_GBF80_MID_0_FAIL);
    }
  }

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_gbf_decode_gbf80_to_float32)
{
  memx_status status = MEMX_STATUS_OK;
  uint8_t gbf80_buffer[20] = {0x21,0xd0,0x57,0x05,0x00,0x00,0x00,0x00,0x00,0x7f,0x09,0xa8,0x2e,0x07,0x00,0x00,0x00,0x00,0x00,0x80};
  float flt32_buffer[16] = {0};
  float flt32_golden[6] = {0.269762,-1.811971,-0.657244,0.145004,-1.310345,-3.164122};

  if(memx_test_result_okay) { // null_0: gbf80=nullptr -> report error
    status = memx_gbf_decode_gbf80_to_float32(NULL, flt32_buffer, 1);
    if(memx_test_assert_equal(status, MEMX_STATUS_GBF_INVALID_DATA)) {
    } else {
      memx_test_result_set(MEMX_TEST_GBF_DECODE_GBF80_TO_FLOAT32_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // null_1: flt32=nullptr -> report error
    status = memx_gbf_decode_gbf80_to_float32(gbf80_buffer, NULL, 1);
    if(memx_test_assert_equal(status, MEMX_STATUS_GBF_INVALID_DATA)) {
    } else {
      memx_test_result_set(MEMX_TEST_GBF_DECODE_GBF80_TO_FLOAT32_NULL_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: length=0 -> do nothing
    status = memx_gbf_decode_gbf80_to_float32(gbf80_buffer, flt32_buffer, 0);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && memx_test_assert_equal(flt32_buffer[0], 0.0)) {
    } else {
      memx_test_result_set(MEMX_TEST_GBF_DECODE_GBF80_TO_FLOAT32_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: length=1 -> golden
    status = memx_gbf_decode_gbf80_to_float32(gbf80_buffer, flt32_buffer, 1);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && ((flt32_golden[0]*0.8 < flt32_buffer[0]) && (flt32_buffer[0] < flt32_golden[0]*1.2))
      && memx_test_assert_equal(flt32_buffer[1], 0.0)) {
    } else {
      memx_test_result_set(MEMX_TEST_GBF_DECODE_GBF80_TO_FLOAT32_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_0: length=8 -> golden
    status = memx_gbf_decode_gbf80_to_float32(gbf80_buffer, flt32_buffer, 8);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && ((flt32_golden[0]*((flt32_golden[0] > 0) ? 0.8 : 1.2) < flt32_buffer[0]) && (flt32_buffer[0] < flt32_golden[0]*((flt32_golden[0] > 0) ? 1.2 : 0.8)))
      && ((flt32_golden[1]*((flt32_golden[1] > 0) ? 0.8 : 1.2) < flt32_buffer[1]) && (flt32_buffer[1] < flt32_golden[1]*((flt32_golden[1] > 0) ? 1.2 : 0.8)))
      && ((flt32_golden[2]*((flt32_golden[2] > 0) ? 0.8 : 1.2) < flt32_buffer[2]) && (flt32_buffer[2] < flt32_golden[2]*((flt32_golden[2] > 0) ? 1.2 : 0.8)))
      && memx_test_assert_equal(flt32_buffer[3], 0.0)
      && memx_test_assert_equal(flt32_buffer[4], 0.0)
      && memx_test_assert_equal(flt32_buffer[5], 0.0)
      && memx_test_assert_equal(flt32_buffer[6], 0.0)
      && memx_test_assert_equal(flt32_buffer[7], 0.0)
      && memx_test_assert_equal(flt32_buffer[8], 0.0)) {
    } else {
      memx_test_result_set(MEMX_TEST_GBF_DECODE_GBF80_TO_FLOAT32_MAX_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_1: length=9 -> golden
    status = memx_gbf_decode_gbf80_to_float32(gbf80_buffer, flt32_buffer, 9);
    if(memx_test_assert_equal(status, MEMX_STATUS_OK)
      && ((flt32_golden[0]*((flt32_golden[0] > 0) ? 0.8 : 1.2) < flt32_buffer[0]) && (flt32_buffer[0] < flt32_golden[0]*((flt32_golden[0] > 0) ? 1.2 : 0.8)))
      && ((flt32_golden[1]*((flt32_golden[1] > 0) ? 0.8 : 1.2) < flt32_buffer[1]) && (flt32_buffer[1] < flt32_golden[1]*((flt32_golden[1] > 0) ? 1.2 : 0.8)))
      && ((flt32_golden[2]*((flt32_golden[2] > 0) ? 0.8 : 1.2) < flt32_buffer[2]) && (flt32_buffer[2] < flt32_golden[2]*((flt32_golden[2] > 0) ? 1.2 : 0.8)))
      && memx_test_assert_equal(flt32_buffer[3], 0.0)
      && memx_test_assert_equal(flt32_buffer[4], 0.0)
      && memx_test_assert_equal(flt32_buffer[5], 0.0)
      && memx_test_assert_equal(flt32_buffer[6], 0.0)
      && memx_test_assert_equal(flt32_buffer[7], 0.0)
      && ((flt32_golden[3]*((flt32_golden[3] > 0) ? 0.8 : 1.2) < flt32_buffer[8]) && (flt32_buffer[8] < flt32_golden[3]*((flt32_golden[3] > 0) ? 1.2 : 0.8)))
      && memx_test_assert_equal(flt32_buffer[9], 0.0)) {
    } else {
      memx_test_result_set(MEMX_TEST_GBF_DECODE_GBF80_TO_FLOAT32_MAX_1_FAIL);
    }
  }

} MEMX_TESTCASE_END;

/***************************************************************************//**
 * test sequence
 ******************************************************************************/
int main(int argc, char** argv)
{
  memx_test_result result = MEMX_TEST_PASS;

  if(memx_test_result_okay)
    result = test_memx_gbf_encode_float32_to_gbf80();
  if(memx_test_result_okay)
    result = test_memx_gbf_decode_gbf80_to_float32();

  unused(argc);
  unused(argv);
  return 0;
}

