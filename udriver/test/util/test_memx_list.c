/***************************************************************************//**
 * @note
 * Copyright (C) 2019-2022 MemryX Limited. All rights reserved.
 *
 ******************************************************************************/
#include "memx_common.h"
#include "memx_status.h"

#include "memx_test.h"
#include "memx_list.h"

/***************************************************************************//**
 * testcase
 ******************************************************************************/
MEMX_TESTCASE(test_memx_list_create)
{
  if(memx_test_result_okay) { // mid_0: none -> return ptr
    MemxList *list = memx_list_create();
    if(memx_test_assert_not_null(list)
      && memx_test_assert_null(list->head)
      && memx_test_assert_null(list->tail)
      && memx_test_assert_equal(list->count, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_CREATE_MID_0_FAIL);
    }
    memx_list_destroy(list);
  }

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_list_push)
{
  int error = 0;

  MemxList *list = memx_list_create();
  if(memx_test_assert_not_null(list)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_LIST_PUSH_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: list=nullptr -> return -1
    error = memx_list_push(NULL, NULL);
    if(memx_test_assert_equal(error, -1)
      && memx_test_assert_null(list->head)
      && memx_test_assert_null(list->tail)
      && memx_test_assert_equal(list->count, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_PUSH_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: push=1 -> return 0
    memx_list_clear(list);
    error = memx_list_push(list, NULL);
    if(memx_test_assert_equal(error, 0)
      && memx_test_assert_not_null(list->head)
      && memx_test_assert_not_null(list->tail)
      && memx_test_assert_equal(list->count, 1)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_PUSH_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_1: push=2 -> return 0
    memx_list_clear(list);
    memx_list_push(list, NULL);
    error = memx_list_push(list, NULL);
    if(memx_test_assert_equal(error, 0)
      && memx_test_assert_not_null(list->head)
      && memx_test_assert_not_null(list->tail)
      && memx_test_assert_equal(list->count, 2)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_PUSH_MID_1_FAIL);
    }
  }

  memx_list_destroy(list); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_list_pop)
{
  typedef struct _CustomData
  {
    int dummy;
  } CustomData;

  MemxList *list = memx_list_create();
  if(memx_test_assert_not_null(list)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_LIST_POP_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: list=nullptr -> return nullptr
    void *data = NULL;
    data = memx_list_pop(NULL);
    if(memx_test_assert_null(data)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_POP_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: push=0,pop=1 -> return nullptr
    CustomData *out;
    memx_list_clear(list);
    out = memx_list_pop(list);
    if(memx_test_assert_null(out)
      && memx_test_assert_null(list->head)
      && memx_test_assert_null(list->tail)
      && memx_test_assert_equal(list->count, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_POP_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: push=pop=1 -> return data
    CustomData data;
    CustomData *out;
    memx_list_clear(list);
    memx_list_push(list, &data);
    out = memx_list_pop(list);
    if(memx_test_assert_not_null(out)
      && memx_test_assert_equal(out, &data)
      && memx_test_assert_null(list->head)
      && memx_test_assert_null(list->tail)
      && memx_test_assert_equal(list->count, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_POP_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_1: push=pop=2 -> return data
    CustomData data_0;
    CustomData data_1;
    CustomData *out_0;
    CustomData *out_1;
    memx_list_clear(list);
    memx_list_push(list, &data_0);
    memx_list_push(list, &data_1);
    out_0 = memx_list_pop(list);
    out_1 = memx_list_pop(list);
    if(memx_test_assert_not_null(out_0)
      && memx_test_assert_equal(out_0, &data_0)
      && memx_test_assert_not_null(out_1)
      && memx_test_assert_equal(out_1, &data_1)
      && memx_test_assert_null(list->head)
      && memx_test_assert_null(list->tail)
      && memx_test_assert_equal(list->count, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_POP_MID_1_FAIL);
    }
  }

  memx_list_destroy(list); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_list_count)
{
  int count = 0;

  typedef struct _CustomData
  {
    int dummy;
  } CustomData;

  MemxList *list = memx_list_create();
  if(memx_test_assert_not_null(list)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_LIST_COUNT_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: list=nullptr -> return 0
    count = memx_list_count(NULL);
    if(memx_test_assert_equal(count, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_COUNT_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: push=0 -> return 0
    memx_list_clear(list);
    count = memx_list_count(list);
    if(memx_test_assert_equal(count, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_COUNT_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: push=1 -> return 1
    CustomData data;
    memx_list_clear(list);
    memx_list_push(list, &data);
    count = memx_list_count(list);
    if(memx_test_assert_equal(count, 1)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_COUNT_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_1: push=pop=2 -> return data-0
    CustomData data_0;
    CustomData data_1;
    memx_list_clear(list);
    memx_list_push(list, &data_0);
    memx_list_push(list, &data_1);
    count = memx_list_count(list);
    if(memx_test_assert_equal(count, 2)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_COUNT_MID_1_FAIL);
    }
  }

  memx_list_destroy(list); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_list_front)
{
  void* user_data = NULL;

  typedef struct _CustomData
  {
    int dummy;
  } CustomData;

  MemxList *list = memx_list_create();
  if(memx_test_assert_not_null(list)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_LIST_FRONT_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: list=nullptr -> return nullptr
    user_data = memx_list_front(NULL);
    if(memx_test_assert_null(user_data)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_FRONT_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: push=0 -> return nullptr
    memx_list_clear(list);
    user_data = memx_list_front(list);
    if(memx_test_assert_null(user_data)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_FRONT_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: push=1 -> return data
    CustomData data;
    memx_list_clear(list);
    memx_list_push(list, &data);
    user_data = memx_list_front(list);
    if(memx_test_assert_not_null(user_data)
      && memx_test_assert_equal(user_data, &data)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_FRONT_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_1: push=pop=2 -> return data
    CustomData data_0;
    CustomData data_1;
    memx_list_clear(list);
    memx_list_push(list, &data_0);
    memx_list_push(list, &data_1);
    user_data = memx_list_front(list);
    if(memx_test_assert_not_null(user_data)
      && memx_test_assert_equal(user_data, &data_0)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_FRONT_MID_1_FAIL);
    }
  }

  memx_list_destroy(list); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_list_peek)
{
  void* user_data = NULL;

  typedef struct _CustomData
  {
    int dummy;
  } CustomData;

  MemxList *list = memx_list_create();
  if(memx_test_assert_not_null(list)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_LIST_PEEK_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: list=nullptr -> return nullptr
    user_data = memx_list_peek(NULL, 0);
    if(memx_test_assert_null(user_data)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_PEEK_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // null_1: peek=-1 -> return nullptr
    user_data = memx_list_peek(NULL, -1);
    if(memx_test_assert_null(user_data)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_PEEK_NULL_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: push=0, peek=0 -> return nullptr
    memx_list_clear(list);
    user_data = memx_list_peek(list, 0);
    if(memx_test_assert_null(user_data)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_PEEK_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_1: push=1, peek=0 -> return data
    CustomData data;
    memx_list_clear(list);
    memx_list_push(list, &data);
    user_data = memx_list_peek(list, 0);
    if(memx_test_assert_not_null(user_data)
      && memx_test_assert_equal(user_data, &data)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_PEEK_MIN_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: push=2, peek=1 -> return data
    CustomData data_0;
    CustomData data_1;
    memx_list_clear(list);
    memx_list_push(list, &data_0);
    memx_list_push(list, &data_1);
    user_data = memx_list_peek(list, 1);
    if(memx_test_assert_not_null(user_data)
      && memx_test_assert_equal(user_data, &data_1)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_PEEK_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // max_0: push=1, peak=1 -> return nullptr
    CustomData data;
    memx_list_clear(list);
    memx_list_push(list, &data);
    user_data = memx_list_peek(list, 1);
    if(memx_test_assert_null(user_data)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_PEEK_MAX_0_FAIL);
    }
  }

  memx_list_destroy(list); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_list_exists)
{
  int exists_0 = 0;
  int exists_1 = 0;

  typedef struct _CustomData
  {
    int dummy;
  } CustomData;

  MemxList *list = memx_list_create();
  if(memx_test_assert_not_null(list)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_LIST_EXISTS_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: list=nullptr -> return 0
    exists_0 = memx_list_exists(list, NULL);
    if(memx_test_assert_equal(exists_0, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_EXISTS_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: push=0 -> return 0
    memx_list_clear(list);
    exists_0 = memx_list_exists(list, NULL);
    if(memx_test_assert_equal(exists_0, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_EXISTS_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: push=1 -> return 1
    CustomData data_0;
    CustomData data_1;
    memx_list_clear(list);
    memx_list_push(list, &data_0);
    exists_0 = memx_list_exists(list, &data_0);
    exists_1 = memx_list_exists(list, &data_1);
    if(memx_test_assert_equal(exists_0, 1)
      && memx_test_assert_equal(exists_1, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_EXISTS_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_1: push=2 -> return 0
    CustomData data_0;
    CustomData data_1;
    memx_list_clear(list);
    memx_list_push(list, &data_0);
    memx_list_push(list, &data_1);
    exists_0 = memx_list_exists(list, &data_0);
    exists_1 = memx_list_exists(list, &data_1);
    if(memx_test_assert_equal(exists_0, 1)
      && memx_test_assert_equal(exists_1, 1)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_EXISTS_MID_1_FAIL);
    }
  }

  memx_list_destroy(list); // clean-up

} MEMX_TESTCASE_END;

MEMX_TESTCASE(test_memx_list_remove)
{
  typedef struct _CustomData
  {
    int dummy;
  } CustomData;

  MemxList *list = memx_list_create();
  if(memx_test_assert_not_null(list)) { // pre-condition
  } else {
    memx_test_result_set(MEMX_TEST_LIST_REMOVE_INIT_FAIL);
  }

  if(memx_test_result_okay) { // null_0: list=nullptr -> return nullptr
    void *data = NULL;
    data = memx_list_remove(NULL, NULL);
    if(memx_test_assert_null(data)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_REMOVE_NULL_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // min_0: push=none,remove=d0 -> return nullptr
    CustomData data_0;
    CustomData* out;
    memx_list_clear(list);
    out = memx_list_remove(list, &data_0);
    if(memx_test_assert_null(out)
      && memx_test_assert_null(list->head)
      && memx_test_assert_null(list->tail)
      && memx_test_assert_equal(list->count, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_REMOVE_MIN_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_0: push=d0,remove=d0 -> return d0
    CustomData data_0;
    CustomData* out;
    memx_list_clear(list);
    memx_list_push(list, &data_0);
    out = memx_list_remove(list, &data_0);
    if(memx_test_assert_not_null(out)
      && memx_test_assert_equal(out, &data_0)
      && memx_test_assert_null(list->head)
      && memx_test_assert_null(list->tail)
      && memx_test_assert_equal(list->count, 0)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_REMOVE_MID_0_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_1: push=d0+d1,remove=d0 -> return d0
    CustomData data_0;
    CustomData data_1;
    CustomData* out;
    memx_list_clear(list);
    memx_list_push(list, &data_0);
    memx_list_push(list, &data_1);
    out = memx_list_remove(list, &data_0);
    if(memx_test_assert_not_null(out)
      && memx_test_assert_equal(out, &data_0)
      && memx_test_assert_not_null(list->head)
      && memx_test_assert_not_null(list->tail)
      && memx_test_assert_equal(list->count, 1)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_REMOVE_MID_1_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_2: push=d0+d1,remove=d1 -> return d1
    CustomData data_0;
    CustomData data_1;
    CustomData* out;
    memx_list_clear(list);
    memx_list_push(list, &data_0);
    memx_list_push(list, &data_1);
    out = memx_list_remove(list, &data_1);
    if(memx_test_assert_not_null(out)
      && memx_test_assert_equal(out, &data_1)
      && memx_test_assert_not_null(list->head)
      && memx_test_assert_not_null(list->tail)
      && memx_test_assert_equal(list->count, 1)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_REMOVE_MID_2_FAIL);
    }
  }
  if(memx_test_result_okay) { // mid_3: push=d0,remove=d1 -> return nullptr
    CustomData data_0;
    CustomData data_1;
    CustomData* out;
    memx_list_clear(list);
    memx_list_push(list, &data_0);
    out = memx_list_remove(list, &data_1);
    if(memx_test_assert_null(out)
      && memx_test_assert_not_null(list->head)
      && memx_test_assert_not_null(list->tail)
      && memx_test_assert_equal(list->count, 1)) {
    } else {
      memx_test_result_set(MEMX_TEST_LIST_REMOVE_MID_3_FAIL);
    }
  }

  memx_list_destroy(list); // clean-up

} MEMX_TESTCASE_END;

/***************************************************************************//**
 * test sequence
 ******************************************************************************/
int main(int argc, char **argv)
{
  memx_test_result result = MEMX_TEST_PASS;

  if(memx_test_result_okay)
    result = test_memx_list_create();
  if(memx_test_result_okay)
    result = test_memx_list_push();
  if(memx_test_result_okay)
    result = test_memx_list_pop();
  if(memx_test_result_okay)
    result = test_memx_list_count();
  if(memx_test_result_okay)
    result = test_memx_list_front();
  if(memx_test_result_okay)
    result = test_memx_list_peek();
  if(memx_test_result_okay)
    result = test_memx_list_exists();
  if(memx_test_result_okay)
    result = test_memx_list_remove();

  unused(argc);
  unused(argv);
  return 0;
}

