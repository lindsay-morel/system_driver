/***************************************************************************//**
 * @note
 * Copyright (C) 2019-2022 MemryX Limited. All rights reserved.
 *
 ******************************************************************************/
#include "memx_common.h"
#include "memx_status.h"

#include "memx_test.h"
#include "memx_mpuio.h"
#include "memx_mpu.h"
#include "memx_cascade.h"

/***************************************************************************//**
 * testcase
 ******************************************************************************/
MEMX_TESTCASE(test_memx_cascade_create)
{
  if(memx_test_result_okay) { // null_0: mpuio=nullptr -> return nullptr
    MemxMpu* cascade = memx_cascade_create(NULL, 1);
    if(memx_test_assert_null(cascade)) {
    } else {
      memx_test_result_set(MEMX_TEST_CASCADE_CREATE_NULL_0_FAIL);
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
    result = test_memx_cascade_create();

  unused(argc);
  unused(argv);
  return 0;
}

