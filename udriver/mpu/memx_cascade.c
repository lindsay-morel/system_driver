/***************************************************************************//**
 * @note
 * Copyright (C) 2019-2022 MemryX Limited. All rights reserved.
 *
 ******************************************************************************/
#include "memx_cascade.h"
#include "memx_mpu_comm.h"
#include "memx_gbf.h"

#include <stdlib.h>

#define _CASCADE_MPU_IFMAP_FLOW_NUMBER (15) // Cascade input port number
#define _CASCADE_MPU_IFMAP_QUEUE_DEFAULT_FRAME_NUMBER (4) // default input feature map queue size for each flow
#define _CASCADE_MPU_IFMAP_RINGBUFFER_FRAME_NUMBER (4) // ifmap ring-buffer size, used in buffering data to interface driver

#define _CASCADE_MPU_OFMAP_FLOW_NUMBER (15) // Cascade output port number
#define _CASCADE_MPU_OFMAP_QUEUE_DEFAULT_FRAME_NUMBER (32) // default output feature map queue size for each flow
#define _CASCADE_MPU_OFMAP_RINGBUFFER_FRAME_NUMBER (4) // ofmap ring-buffer size, used in buffering data from interface driver
#define _CASCADE_MPU_OFMAP_ENQUEUE_TIMEOUT (10*1000) // ofmap enqueue timeout(sec), used in enqueued decoded data to user

#define _CASCADE_MPU_JOB_QUEUE_DEFAULT_NUMBER (2) // default number of background job queue
#define _CASCADE_MPU_JOB_WORKER_DEFAULT_NUMBER (1) // default number of background job worker for each job queue

/**
 * @brief Cascade context which stores device dependent information.
 */
typedef struct _MemxCascade
{
  MemxMpuIfmapInfo ifmap_infos[_CASCADE_MPU_IFMAP_FLOW_NUMBER]; // input feature map configuration
  MemxList *ifmap_queues[_CASCADE_MPU_IFMAP_FLOW_NUMBER]; // input feature map frame-based queue descriptor
  MemxMpuFmapRingBuffer ifmap_ringbuffers[_CASCADE_MPU_IFMAP_FLOW_NUMBER]; // input feature map row-based ring-buffer descriptor

  MemxMpuOtaCounter ifmap_ota_entry_count; // input feature map on-the-air counter used to record total number of entries in all flows' ring-buffer
  MemxMpuWorkerInfo ifmap_worker_info; // input feature map background worker, push data from ring-buffer to interface driver
  int32_t ifmap_queue_size; // input feature map maximum queue size

  MemxMpuOfmapInfo ofmap_infos[_CASCADE_MPU_OFMAP_FLOW_NUMBER]; // output feature map configuration
  MemxList *ofmap_queues[_CASCADE_MPU_OFMAP_FLOW_NUMBER]; // output feature map frame-based queue descriptor
  MemxMpuFmapRingBuffer ofmap_ringbuffers[_CASCADE_MPU_OFMAP_FLOW_NUMBER]; // output feature map row-based ring-buffer descriptor
  //MemxMpuOtaCounter ofmap_ota_entry_count; // output feature map on-the-air counter used to record total number of entries in all flows' ring-buffer
  MemxMpuWorkerInfo ofmap_worker_info; // output feature map background worker, poll data from interface driver to ring-buffer
  int32_t ofmap_queue_size; // output feature map maximum queue size

  MemxMpuJobQueue ifmap_job_queue; // ifmap job queue stores job to be executed
  MemxMpuJobQueue ofmap_job_queue; // ofmap job queue stores job to be executed
  MemxList *ifmap_job_worker_infos; // ifmap job background workers, number is dynamically adjustable
  MemxList *ofmap_job_worker_infos; // ofmap job background workers, number is dynamically adjustable
  int32_t job_worker_number; // job background worker number
} MemxCascade;

/***************************************************************************//**
 * internal helper
 ******************************************************************************/

/**
 * @brief Do frame queue to row ring-buffer format encoding. Keeps retrying and
 * waiting until whole frame is completely encoded and been put to ring-buffer.
 *
 * @param job_worker_info     job worker control information
 * @param flow_id             target flow ID
 *
 * @return 0 on success, otherwise error code
 */
memx_status _memx_cascade_job_ifmap_encode(MemxMpuWorkerInfo *job_worker_info, uint8_t flow_id)
{
    if (!job_worker_info || !job_worker_info->mpu || !job_worker_info->mpu->context) {
        printf("NULL arguments check at line(%d) status(%d)\n", __LINE__, MEMX_STATUS_MPU_INVALID_CONTEXT);
        return MEMX_STATUS_MPU_INVALID_CONTEXT;
    }
    MemxCascade *cascade = (MemxCascade *)job_worker_info->mpu->context;
    if (flow_id >= _CASCADE_MPU_IFMAP_FLOW_NUMBER) {
        printf("invaild flow id(%d) check at line(%d) status(%d)\n", flow_id, __LINE__, MEMX_STATUS_MPU_INVALID_FLOW_ID);
        return MEMX_STATUS_MPU_INVALID_FLOW_ID;
    }
    MemxMpuIfmapInfo *ifmap_info = &cascade->ifmap_infos[flow_id];
    MemxMpuFmapRingBuffer *ifmap_ringbuffer = &cascade->ifmap_ringbuffers[flow_id];

     // ifmap ring buffer local reference
    int32_t max_num_of_entry_by_hight = ifmap_ringbuffer->Cascade.max_num_of_entry_by_hight;
    size_t size_of_one_entry_by_row = ifmap_ringbuffer->Cascade.size_of_one_entry_by_row;
    uint8_t *ring_buffer_data_start = ifmap_ringbuffer->Cascade.data;

    // lock target ring-buffer to prevent other thread from pushing data
    if (job_worker_info->enable == 1) {
        // de-queue input feature map from user
        void *ifmap_data = memx_list_pop(cascade->ifmap_queues[flow_id]);
        if (!ifmap_data) {
            printf("cascade->ifmap_queues[flow_id(%d)] pop fail\n", flow_id);
            return MEMX_STATUS_MPU_IFMAP_DEQUEUE_FAIL;
        }

        platform_mutex_lock(&ifmap_ringbuffer->Cascade.read_guard);

        size_t queue_data_offset = 0; // from queue
        size_t ring_buffer_data_offset = ifmap_ringbuffer->Cascade.entry_w * size_of_one_entry_by_row; // to ring-buffer
        // encode by row to reduce end-to-end latency
        for (int32_t row = 0; row < ifmap_info->fmap.height; ++row) {
            // make sure all read and write are latest status.
            // wait until there is at least one row space in ring-buffer available
            pthread_wait_until_condition_flase_with_lock(((ifmap_ringbuffer->Cascade.entry_w + 1) % max_num_of_entry_by_hight == ifmap_ringbuffer->Cascade.entry_r), &ifmap_ringbuffer->Cascade.pointer_changed, &ifmap_ringbuffer->Cascade.guard);

            // based on input feature map format to choose encoding method
            switch (ifmap_info->fmap.format) {
                case MEMX_MPU_IFMAP_FORMAT_FLOAT32: {
                    size_t flt32_pixel_offset = 0;
                    size_t gbf80_pixel_offset = 0;
                    for (int32_t j = 0; j < ifmap_info->fmap.width; ++j) { // encode by pixel
                        float *from_flt32_buffer = (float *)ifmap_data + queue_data_offset + flt32_pixel_offset;
                        uint8_t *to_gbf80_buffer = ring_buffer_data_start + ring_buffer_data_offset + gbf80_pixel_offset;
                        int32_t length = ifmap_info->fmap.channel_number; // encode only given channel number with no dummy channel
                        memx_gbf_encode_float32_to_gbf80(from_flt32_buffer, to_gbf80_buffer, length);
                        flt32_pixel_offset += ifmap_info->fmap.channel_number;
                        gbf80_pixel_offset += ifmap_info->fmap.fmap_pixel_size;
                    }
                    queue_data_offset += ifmap_info->fmap.row_size; // user data is float32 with no padding
                    ring_buffer_data_offset += ifmap_info->fmap.fmap_row_size;
                } break;
                case MEMX_MPU_IFMAP_FORMAT_FLOAT32_RGB888: {
                    size_t flt32_pixel_offset = 0;
                    size_t rgb_pixel_offset = 0;
                    for (int32_t j = 0; j < ifmap_info->fmap.width; ++j) { // encode by pixel
                        float *flt32 = (float *)ifmap_data + queue_data_offset + flt32_pixel_offset;
                        uint8_t *rgb = ring_buffer_data_start + ring_buffer_data_offset + rgb_pixel_offset;
                        for (int32_t k = 0; k < ifmap_info->fmap.channel_number; ++k) {
                            *(rgb + k) = (uint8_t)((*(flt32 + k) + ifmap_info->range_convert_shift) * ifmap_info->range_convert_scale);
                        }
                        flt32_pixel_offset += ifmap_info->fmap.channel_number;
                        rgb_pixel_offset += ifmap_info->fmap.fmap_pixel_size;
                    }
                    queue_data_offset += ifmap_info->fmap.row_size; // user data is float32 with no padding
                    ring_buffer_data_offset += ifmap_info->fmap.fmap_row_size;
                } break;
                case MEMX_MPU_IFMAP_FORMAT_GBF80: {
                    void *to_gbf80_dst = ring_buffer_data_start + ring_buffer_data_offset;
                    const void *from_gbf80_src = (uint8_t *)ifmap_data + queue_data_offset;
                    size_t size_with_no_padding = ifmap_info->fmap.row_size;

                    memcpy(to_gbf80_dst, from_gbf80_src, size_with_no_padding);
                    queue_data_offset += ifmap_info->fmap.row_size; // user data is gbf80 with no padding
                    ring_buffer_data_offset += ifmap_info->fmap.fmap_row_size;
                } break;
                default: { // MEMX_MPU_IFMAP_FORMAT_RAW
                    void *to_byte_dst = ring_buffer_data_start + ring_buffer_data_offset;
                    const void *from_byte_src = (uint8_t *)ifmap_data + queue_data_offset;
                    size_t size_with_no_padding = ifmap_info->fmap.row_size;

                    memcpy(to_byte_dst, from_byte_src, size_with_no_padding);
                    queue_data_offset += ifmap_info->fmap.row_size; // user data is byte array with no padding
                    ring_buffer_data_offset += ifmap_info->fmap.fmap_row_size;
                }
            }

            // update ring-buffer pointer by row
            ifmap_ringbuffer->Cascade.entry_w = ((ifmap_ringbuffer->Cascade.entry_w + 1) == max_num_of_entry_by_hight) ? 0 : ifmap_ringbuffer->Cascade.entry_w + 1;

            // signal on-the-air ifmap entry counter's value changed
            _memx_mpu_ota_counter_increase(cascade->ifmap_ota_entry_count, 1);
        }
        platform_mutex_unlock(&ifmap_ringbuffer->Cascade.read_guard);
        // release resource allocated
        free(ifmap_data);
        ifmap_data = NULL;
    }
    return MEMX_STATUS_OK;
}

/**
 * @brief Do row ring-buffer to frame queue format decoding. Keeps retrying and
 * waiting until whole frame is completely received and be decoded to queue.
 *
 * @param job_worker_info     job worker control information
 * @param flow_id             target flow ID
 *
 * @return 0 on success, otherwise error code
 */
memx_status _memx_cascade_job_ofmap_decode(MemxMpuWorkerInfo *job_worker_info, uint8_t flow_id)
{
    if (!job_worker_info || !job_worker_info->mpu || !job_worker_info->mpu->context) {
        printf("NULL arguments check at line(%d) status(%d)\n", __LINE__, MEMX_STATUS_MPU_INVALID_CONTEXT);
        return MEMX_STATUS_MPU_INVALID_CONTEXT;
    }
    if (flow_id >= _CASCADE_MPU_OFMAP_FLOW_NUMBER) {
        printf("invaild flow id(%d) check at line(%d) status(%d)\n", flow_id, __LINE__, MEMX_STATUS_MPU_INVALID_FLOW_ID);
        return MEMX_STATUS_MPU_INVALID_FLOW_ID;
    }

    MemxCascade *cascade = (MemxCascade *)job_worker_info->mpu->context;
    MemxMpuOfmapInfo *ofmap_info = &cascade->ofmap_infos[flow_id];
    MemxMpuFmapRingBuffer *ofmap_ringbuffer = &cascade->ofmap_ringbuffers[flow_id];

    // ofmap ring buffer local reference
    int32_t max_num_of_entry_by_hight = ofmap_ringbuffer->Cascade.max_num_of_entry_by_hight;
    size_t size_of_one_entry_by_row = ofmap_ringbuffer->Cascade.size_of_one_entry_by_row;
    uint8_t *ring_buffer_data_start = ofmap_ringbuffer->Cascade.data;

    // lock target ring-buffer to prevent other thread to take data
    if (job_worker_info->enable == 1) {
       // allocate output feature map buffer
        // special usage for gbf80 to use size after padding
        void *ofmap_data = malloc((int64_t)(ofmap_info->Cascade.fmap.user_frame_size) * ofmap_info->Cascade.fmap.channel_size);
        if (!ofmap_data) {
            printf("memory allocate for ofmap_data fail\n");
            return MEMX_STATUS_MPU_OFMAP_ALLOCATE_FAIL;
        }
        platform_mutex_lock(&ofmap_ringbuffer->Cascade.read_guard);
        // decode by row to reduce end-to-end latency
        size_t ring_buffer_data_offset = ofmap_ringbuffer->Cascade.entry_r * size_of_one_entry_by_row; // from ring-buffer
        size_t queue_data_offset = 0; // to queue
        for (int32_t row = 0; row < ofmap_info->Cascade.fmap.height; ++row) {
            // based on output feature map format to choose decoding method
            switch (ofmap_info->Cascade.fmap.format) {
                case MEMX_MPU_OFMAP_FORMAT_FLOAT32: {
                    size_t gbf80_pixel_offset = 0;
                    size_t flt32_pixel_offset = 0;
                    for (int32_t j = 0; j < ofmap_info->Cascade.fmap.width; ++j) { // decode by pixel
                        uint8_t *gbf80_buffer = ring_buffer_data_start + ring_buffer_data_offset + gbf80_pixel_offset;
                        float *flt32_buffer = (float *)ofmap_data + queue_data_offset + flt32_pixel_offset;
                        int32_t length = ofmap_info->Cascade.fmap.channel_number; // decode only given channel_number with no dummy channel

                        memx_gbf_decode_gbf80_to_float32(gbf80_buffer, flt32_buffer, length);
                        gbf80_pixel_offset += ofmap_info->Cascade.fmap.fmap_pixel_size;
                        flt32_pixel_offset += ofmap_info->Cascade.fmap.channel_number;
                    }
                    ring_buffer_data_offset += ofmap_info->Cascade.fmap.fmap_row_size;
                    queue_data_offset += ofmap_info->Cascade.fmap.row_size; // user data is float32 with no padding
                } break;
                case MEMX_MPU_OFMAP_FORMAT_FLOAT32_HPOC: {
                    // Deocde to float, user need remove dummy channel.

                    size_t gbf80_offset = 0; // raw data buffer byte offset which includes dummy channels
                    for (int32_t gbf_blk_idx = 0; gbf_blk_idx < ofmap_info->Cascade.fmap.width; ++gbf_blk_idx) {
                        int32_t check_dummy_channel_idx = 0;
                        for (int32_t channel_blk_ofst = 0, gbf_blk_ofst = 0; channel_blk_ofst < ofmap_info->Cascade.fmap.fmap_channel_number;
                                                                                    channel_blk_ofst += 8, gbf_blk_ofst += 10) {
                            float decode_float_buf[8] = {0};
                            memx_gbf_decode_gbf80_to_float32(ring_buffer_data_start + ring_buffer_data_offset + gbf80_offset + gbf_blk_ofst, decode_float_buf, 8);
                            for (int32_t channel_offset = 0; channel_offset < 8; ++channel_offset) {
                                size_t flt32_offset = 0; // user buffer float offset

                                int32_t channel_index = channel_blk_ofst + channel_offset;
                                if ((check_dummy_channel_idx <  ofmap_info->Cascade.hpoc_dummy_channel_array_size) &&
                                    (channel_index == ofmap_info->Cascade.hpoc_dummy_channel_indexes[check_dummy_channel_idx])) {
                                    check_dummy_channel_idx++;
                                    continue;
                                } else {
                                    // skip extra dummy channel if any
                                    if (channel_blk_ofst + channel_offset < ofmap_info->Cascade.hpoc_dummy_channel_number) {
                                        *((float*)ofmap_data + queue_data_offset + flt32_offset) = decode_float_buf[channel_offset];
                                        flt32_offset++;
                                    }
                                }
                                queue_data_offset += flt32_offset;
                            }
                        }
                        gbf80_offset += ofmap_info->Cascade.fmap.fmap_pixel_size;
                    }
                    ring_buffer_data_offset += ofmap_info->Cascade.fmap.fmap_row_size;
                } break;
                case MEMX_MPU_OFMAP_FORMAT_GBF80: {
                    void *to_gbf80_dst = (uint8_t *)ofmap_data + queue_data_offset;
                    const void *from_gbf80_src = ring_buffer_data_start + ring_buffer_data_offset;
                    size_t size_with_no_padding = ofmap_info->Cascade.fmap.row_size;
                    memcpy(to_gbf80_dst, from_gbf80_src, size_with_no_padding);
                    ring_buffer_data_offset += ofmap_info->Cascade.fmap.fmap_row_size;
                    queue_data_offset += ofmap_info->Cascade.fmap.row_size; // user data is gbf80 with no padding
                } break;
                default: { // MEMX_MPU_OFMAP_FORMAT_RAW
                    void *to_byte_dst = (uint8_t *)ofmap_data + queue_data_offset;
                    const void *from_byte_src = ring_buffer_data_start + ring_buffer_data_offset;
                    size_t size_with_no_padding = ofmap_info->Cascade.fmap.row_size;
                    memcpy(to_byte_dst, from_byte_src, size_with_no_padding);
                    ring_buffer_data_offset += ofmap_info->Cascade.fmap.fmap_row_size;
                    queue_data_offset += ofmap_info->Cascade.fmap.row_size; // user data is byte array with no padding
                }
            }

            // update ring-buffer pointer by row
            ofmap_ringbuffer->Cascade.entry_r = ((ofmap_ringbuffer->Cascade.entry_r + 1) == max_num_of_entry_by_hight) ? 0 : ofmap_ringbuffer->Cascade.entry_r + 1;
        }
        platform_mutex_unlock(&ofmap_ringbuffer->Cascade.read_guard);

        int32_t timeout = 0;
        while (memx_list_count(cascade->ofmap_queues[flow_id]) >= cascade->ofmap_queue_size) { // full
            platform_usleep(1000);
            if (timeout++ > _CASCADE_MPU_OFMAP_ENQUEUE_TIMEOUT){
                // clean-up on error
                free(ofmap_data);
                ofmap_data = NULL;
                printf("en-queue ofmap_data to cascade->ofmap_queues[flow_id(%d)]) timeout\n", flow_id);
                return MEMX_STATUS_MPU_OFMAP_ENQUEUE_FAIL;
            }
        }

        // en-queue output feature map to user
        if (memx_list_push(cascade->ofmap_queues[flow_id], ofmap_data)) {
            // clean-up on error
            free(ofmap_data);
            ofmap_data = NULL;
            printf("en-queue ofmap_data to cascade->ofmap_queues[flow_id(%d)]) fail\n", flow_id);
            return MEMX_STATUS_MPU_OFMAP_ENQUEUE_FAIL;
        }
    }
    return MEMX_STATUS_OK;
}

/**
 * @brief Helper function to create new ifmap job and put it to queue. Job created
 * should be released later.
 *
 * @param mpu                 MPU context
 * @param flow_id             target flow ID
 * @param action              function to be executed
 *
 * @return 0 on success, otherwise error code
 */
memx_status _memx_cascade_ifmap_job_enqueue(MemxMpu *mpu, uint8_t flow_id, job_action_t action)
{
    if (!mpu || !mpu->context || !action) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    if (flow_id >= _CASCADE_MPU_IFMAP_FLOW_NUMBER) { return MEMX_STATUS_MPU_INVALID_FLOW_ID; }

    MemxCascade *cascade = (MemxCascade *)mpu->context;
    MemxMpuJob *job = (MemxMpuJob *)malloc(sizeof(MemxMpuJob));
    if (!job) {
        printf("Memory allocate for MemxMpuJob fail\n");
        return MEMX_STATUS_MPU_JOB_CREATE_FAIL;
    }
    job->flow_id = flow_id;
    job->action = action;
    if (memx_list_push(cascade->ifmap_job_queue.queue, job)) {
        printf("enqueue job to cascade->ifmap_job_queue.queue fail\n");
        return MEMX_STATUS_MPU_JOB_ENQUEUE_FAIL;
    }
    pthread_cond_signal_with_lock(&cascade->ifmap_job_queue.enqueue, &cascade->ifmap_job_queue.guard);

    return MEMX_STATUS_OK;
}

/**
 * @brief Helper function to create new ofmap job and put it to queue. Job created
 * should be released later.
 *
 * @param mpu                 MPU context
 * @param flow_id             target flow ID
 * @param action              function to be executed
 *
 * @return 0 on success, otherwise error code
 */
memx_status _memx_cascade_ofmap_job_enqueue(MemxMpu* mpu, uint8_t flow_id, job_action_t action)
{
    if (!mpu || !mpu->context || !action) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    if (flow_id >= _CASCADE_MPU_IFMAP_FLOW_NUMBER) { return MEMX_STATUS_MPU_INVALID_FLOW_ID; }

    MemxCascade *cascade = (MemxCascade *)mpu->context;
    MemxMpuJob *job = (MemxMpuJob *)malloc(sizeof(MemxMpuJob));
    if (!job) {
        printf("Memory allocate for MemxMpuJob fail\n");
        return MEMX_STATUS_MPU_JOB_CREATE_FAIL;
    }
    job->flow_id = flow_id;
    job->action = action;
    if (memx_list_push(cascade->ofmap_job_queue.queue, job)) {
        printf("enqueue job to cascade->ofmap_job_queue.queue fail\n");
        return MEMX_STATUS_MPU_JOB_ENQUEUE_FAIL;
    }
    pthread_cond_signal_with_lock(&cascade->ofmap_job_queue.enqueue, &cascade->ofmap_job_queue.guard);
    return MEMX_STATUS_OK;
}

/**
 * @brief Background worker helps to move data from queue to ring-buffer
 *
 * @param user_data           job worker control information
 *
 * @return none
 */
void* _memx_cascade_ifmap_job_worker_dowork(void* user_data)
{
    memx_status status = MEMX_STATUS_OK;
    MemxMpuWorkerInfo* job_worker_info = (MemxMpuWorkerInfo*)user_data;
    MemxCascade* cascade = (MemxCascade*)job_worker_info->mpu->context;
    MemxMpuJob* job = NULL;

    // infinite loop, stops only when program is terminated
    while(job_worker_info->enable == 1)
    {
        // go to sleep on demand
        _memx_mpu_worker_go_to_sleep(job_worker_info);

        // pop job from job queue if there is any
        job = (MemxMpuJob*)memx_list_pop(cascade->ifmap_job_queue.queue);
        if((job_worker_info->enable == 1)&&(job != NULL)) {
            status = job->action(job_worker_info, job->flow_id); // execution
            if(memx_status_error(status))
                printf("memx_cascade: job-worker error = %d\n", status);
            free(job); // release job allocated
        }

        // go to sleep if nothing in queue
        pthread_wait_until_condition_flase_with_lock((job_worker_info->enable == 1)
            &&(job_worker_info->state_next == _mpu_worker_state_running)
            &&(memx_list_count(cascade->ifmap_job_queue.queue) == 0),
            &cascade->ifmap_job_queue.enqueue, &cascade->ifmap_job_queue.guard);
    }

    return NULL;
}

/**
 * @brief Background worker helps to move data from ring-buffer to queue.
 *
 * @param user_data           job worker control information
 *
 * @return none
 */
void* _memx_cascade_ofmap_job_worker_dowork(void* user_data)
{
    memx_status status = MEMX_STATUS_OK;
    MemxMpuWorkerInfo* job_worker_info = (MemxMpuWorkerInfo*)user_data;
    MemxCascade* cascade = (MemxCascade*)job_worker_info->mpu->context;
    MemxMpuJob* job = NULL;

    // infinite loop, stops only when program is terminated
    while(job_worker_info->enable == 1)
    {
        // go to sleep on demand
        _memx_mpu_worker_go_to_sleep(job_worker_info);

        // pop job from job queue if there is any
        job = (MemxMpuJob*)memx_list_pop(cascade->ofmap_job_queue.queue);
        if((job_worker_info->enable == 1)&&(job != NULL)) {
            status = job->action(job_worker_info, job->flow_id); // execution
            if(memx_status_error(status))
                printf("memx_cascade: job-worker error = %d\n", status);
            free(job); // release job allocated
        }

        // go to sleep if nothing in queue
        pthread_wait_until_condition_flase_with_lock((job_worker_info->enable == 1)
            &&(job_worker_info->state_next == _mpu_worker_state_running)
            &&(memx_list_count(cascade->ofmap_job_queue.queue) == 0),
            &cascade->ofmap_job_queue.enqueue, &cascade->ofmap_job_queue.guard);
    }

    return NULL;
}

/**
 * @brief Calculates maximum number of entries available within ring-buffer
 * and send to interface driver. In order to utilize interface's bandwidth, try
 * to transfer as much data as possible at once.
 *
 * @param ifmap_worker_info   ifmap worker control information
 * @param flow_id             target flow ID
 *
 * @return byte size transferred
 */
size_t _memx_cascade_ifmap_worker_transfer(MemxMpuWorkerInfo *ifmap_worker_info, uint8_t flow_id)
{
    if (!ifmap_worker_info) { printf("ifmap_worker_transfer: worker_info is NULL\n"); return 0; }
    if (flow_id >= _CASCADE_MPU_IFMAP_FLOW_NUMBER) { printf("ifmap_worker_transfer: flow_id(%d) is invaild\n", flow_id); return 0; }
    if (!ifmap_worker_info->mpu || !ifmap_worker_info->mpu->context) { printf("ifmap_worker_transfer: mpu or context is NULL\n"); return 0; }

    MemxMpu *mpu = ifmap_worker_info->mpu;
    MemxCascade *cascade = (MemxCascade *)ifmap_worker_info->mpu->context;
    MemxMpuFmapRingBuffer *ifmap_ringbuffer = &cascade->ifmap_ringbuffers[flow_id];
    int32_t max_num_of_entry_by_hight = ifmap_ringbuffer->Cascade.max_num_of_entry_by_hight;
    size_t size_of_one_entry_by_row = ifmap_ringbuffer->Cascade.size_of_one_entry_by_row;
    uint8_t *ring_buffer_data_start = ifmap_ringbuffer->Cascade.data;

    int32_t nume_of_entry_need_process = 0;
    platform_mutex_lock(&ifmap_ringbuffer->Cascade.write_guard);
    if (ifmap_ringbuffer->Cascade.entry_w > ifmap_ringbuffer->Cascade.entry_r) {
        // legal normal case :
        nume_of_entry_need_process = ifmap_ringbuffer->Cascade.entry_w - ifmap_ringbuffer->Cascade.entry_r;
    } else if (ifmap_ringbuffer->Cascade.entry_w < ifmap_ringbuffer->Cascade.entry_r) {
        // legal wrapper case :
        nume_of_entry_need_process = max_num_of_entry_by_hight - ifmap_ringbuffer->Cascade.entry_r;
    } else {
        // illegal case : skip this transfer process for this ring buffer
        printf("illegal expected case happen\n");
        return 0;
    }
    platform_mutex_unlock(&ifmap_ringbuffer->Cascade.write_guard);
    // try to transfer as much data as possible at once
    size_t expected_total_transfer_size = nume_of_entry_need_process * size_of_one_entry_by_row;
    size_t data_buffer_offset = ifmap_ringbuffer->Cascade.entry_r * size_of_one_entry_by_row;

    size_t actually_total_transfer_size = 0;
    // once transfer has began, let it finish or be stopped by user
    while (actually_total_transfer_size < expected_total_transfer_size) {
        int32_t transferred_length = 0;
        uint8_t *data_buffer = ring_buffer_data_start + data_buffer_offset + actually_total_transfer_size;
        int32_t data_length = (int32_t)(expected_total_transfer_size - actually_total_transfer_size);
        // for windows driver, it may sleep for Platform_Write API
        memx_mpuio_stream_write(mpu->mpuio, mpu->input_chip_id, flow_id, data_buffer, data_length, &transferred_length, 0);
        actually_total_transfer_size += transferred_length;
        // TODO: delay a bit if interface driver did not take data
        if (transferred_length == 0) {
            platform_usleep(100);
        }
    }
    // update pointer only if data has been transferred successfully
    platform_mutex_lock(&ifmap_ringbuffer->Cascade.write_guard);
    for (int32_t i = 0; i < nume_of_entry_need_process; i++) {
        ifmap_ringbuffer->Cascade.entry_r = ((ifmap_ringbuffer->Cascade.entry_r + 1) == ifmap_ringbuffer->Cascade.max_num_of_entry_by_hight) ?
                                                                                    0 : ifmap_ringbuffer->Cascade.entry_r + 1;
        // signal ifmap ringbuffer poniter changed to ifmap encode job
        pthread_cond_signal_with_lock(&ifmap_ringbuffer->Cascade.pointer_changed, &ifmap_ringbuffer->Cascade.guard);
        // signal on-the-air ifmap entry counter's value changed
        _memx_mpu_ota_counter_decrease(cascade->ifmap_ota_entry_count, 1);

    }
    platform_mutex_unlock(&ifmap_ringbuffer->Cascade.write_guard);

    // signal ifmap ringbuffer poniter changed to ifmap encode job
//    pthread_cond_signal_with_lock(&ifmap_ringbuffer->Cascade.pointer_changed, &ifmap_ringbuffer->Cascade.guard);
    // signal on-the-air ifmap entry counter's value changed
//    _memx_cascade_ota_counter_decrease(cascade->ifmap_ota_entry_count, nume_of_entry_need_process);
    return actually_total_transfer_size;
}

/**
 * @brief Background worker helps to poll data out from all flow's ring-buffer
 * and send to interface driver.
 *
 * @param user_data           ifmap worker control information
 *
 * @return none
 */
void *_memx_cascade_ifmap_worker_dowork(void *user_data)
{
    if (!user_data) { return NULL; }
    MemxMpuWorkerInfo *ifmap_worker_info = (MemxMpuWorkerInfo *)user_data;
    if (!ifmap_worker_info->mpu || !ifmap_worker_info->mpu->context) { return NULL; }
    MemxCascade *cascade = (MemxCascade *)ifmap_worker_info->mpu->context;

    // infinite loop, stops only when program is terminated
    while (1) {
        // go to sleep on demand
        _memx_mpu_worker_go_to_sleep(ifmap_worker_info);
        // do round-robin polling from each ifmap ring-buffer
        for (uint8_t flow_id = 0; flow_id < _CASCADE_MPU_IFMAP_FLOW_NUMBER; ++flow_id) {
            MemxMpuFmapRingBuffer *ifmap_ringbuffer = &cascade->ifmap_ringbuffers[flow_id];
            if ((ifmap_ringbuffer->Cascade.valid == 1) &&
                (ifmap_ringbuffer->Cascade.entry_w != ifmap_ringbuffer->Cascade.entry_r)) { // not empty
                size_t transferred_length = _memx_cascade_ifmap_worker_transfer(ifmap_worker_info, flow_id);
                if (!transferred_length) {
                    printf("_memx_cascade_ifmap_worker_transfer[flow_id(%d)]: fail\n", flow_id);
                }
            }
        }
        // if the thread disable, we need to make sure it exit without wait bellow
        if (ifmap_worker_info->enable == 0) { break; }

        // only during the thread enable,we need to go to sleep if nothing to be transferred
        // wait for ifmap on-the-air entry counter's value changed
        pthread_wait_until_condition_flase_with_lock((ifmap_worker_info->enable == 1)
            &&(ifmap_worker_info->state_next == _mpu_worker_state_running)
            &&(cascade->ifmap_ota_entry_count.value == 0),
            &cascade->ifmap_ota_entry_count.value_changed,
            &cascade->ifmap_ota_entry_count.guard);
    }
    return NULL;
}

/**
 * @brief Calculates maximum number of entries available within ring-buffer
 * and ask data from interface driver. Try to ask as much data as possible at
 * once to increase decode performance, especially in case output channel
 * number is pretty small.
 *
 * @param ofmap_worker_info   ofmap worker control information
 * @param flow_id             target flow ID
 *
 * @return byte size transferred
 */
size_t _memx_cascade_ofmap_worker_transfer(MemxMpuWorkerInfo *ofmap_worker_info, uint8_t flow_id)
{
    if (!ofmap_worker_info) { printf("ofmap_worker_transfer: ofmap_worker_info is NULL\n"); return 0; }
    if (flow_id >= _CASCADE_MPU_OFMAP_FLOW_NUMBER) { printf("ofmap_worker_transfer: invild flow_id(%d)\n", flow_id); return 0; }
    if (!ofmap_worker_info->mpu || !ofmap_worker_info->mpu->context) {  printf("ofmap_worker_transfer: mpu or context is NULL\n"); return 0; }

    memx_status status = MEMX_STATUS_OK;
    MemxMpu *mpu = ofmap_worker_info->mpu;
    MemxCascade *cascade = (MemxCascade *)ofmap_worker_info->mpu->context;
    MemxMpuOfmapInfo *ofmap_info = &cascade->ofmap_infos[flow_id];
    MemxMpuFmapRingBuffer *ofmap_ringbuffer = &cascade->ofmap_ringbuffers[flow_id];
    int32_t max_num_of_entry_by_hight = ofmap_ringbuffer->Cascade.max_num_of_entry_by_hight;
    size_t size_of_one_entry_by_row = ofmap_ringbuffer->Cascade.size_of_one_entry_by_row;
    uint8_t *ring_buffer_data_start = ofmap_ringbuffer->Cascade.data;

    int32_t nume_of_entry_need_process = 0;
    platform_mutex_lock(&ofmap_ringbuffer->Cascade.read_guard);
    if (((ofmap_ringbuffer->Cascade.entry_w + 1) % max_num_of_entry_by_hight) == ofmap_ringbuffer->Cascade.entry_r) {
        // illegal case : skip this transfer process
        printf("illegal case\n");
        return 0;
    } else if (ofmap_ringbuffer->Cascade.entry_w >= ofmap_ringbuffer->Cascade.entry_r) {
        nume_of_entry_need_process = max_num_of_entry_by_hight - ofmap_ringbuffer->Cascade.entry_w - ((ofmap_ringbuffer->Cascade.entry_r == 0) ? 1 : 0);
    } else {
        nume_of_entry_need_process = ofmap_ringbuffer->Cascade.entry_r - ofmap_ringbuffer->Cascade.entry_w - 1;
    }
    platform_mutex_unlock(&ofmap_ringbuffer->Cascade.read_guard);
    // try to read as much as possible from driver to reduce memcpy latency
    // always reserve 'one' row to avoid empty and full ambiguous
    size_t current_remaining_expected_transfer_total_size = nume_of_entry_need_process * size_of_one_entry_by_row;
    // try to ask for available size of data and get actual size transferred
    size_t data_buffer_offset = ofmap_ringbuffer->Cascade.entry_w * size_of_one_entry_by_row;
    size_t acctually_total_transferred_size = 0;
    if (current_remaining_expected_transfer_total_size) {
        int32_t transferred = 0;
        uint8_t *ring_buffer_data = ring_buffer_data_start + data_buffer_offset;
        int32_t data_length = (int32_t)(current_remaining_expected_transfer_total_size);

        status = memx_mpuio_stream_read(mpu->mpuio, mpu->output_chip_id, flow_id, ring_buffer_data, data_length, &transferred, 1);
        if (memx_status_error(status)) { // debug message on error
            printf("memx_mpuio_stream_read fail at line(%d), status(%d)\n", __LINE__, status);
        }
        acctually_total_transferred_size += transferred;
    }

    // based on Cascade's design, output data should always be row alignment
    // if first transfer is not row-aligned, transfer is split into multiple transfers
    current_remaining_expected_transfer_total_size = acctually_total_transferred_size % size_of_one_entry_by_row;
    current_remaining_expected_transfer_total_size = ((acctually_total_transferred_size == 0) ||
                                                      (current_remaining_expected_transfer_total_size == 0)) ?
                                                     0 : size_of_one_entry_by_row - current_remaining_expected_transfer_total_size;

    size_t non_aligned_actully_total_transferred_size = 0;
    while (non_aligned_actully_total_transferred_size < current_remaining_expected_transfer_total_size) {
        int32_t transferred = 0;
        uint8_t *ring_buffer_data = ring_buffer_data_start + data_buffer_offset + acctually_total_transferred_size;
        int32_t data_length = (int32_t)(current_remaining_expected_transfer_total_size - non_aligned_actully_total_transferred_size);
        status = memx_mpuio_stream_read(mpu->mpuio, mpu->output_chip_id, flow_id, ring_buffer_data, data_length, &transferred, 1);
        if (memx_status_error(status)) { // debug message on error
            printf("memx_mpuio_stream_read fail at line(%d), status(%d)\n", __LINE__, status);
        }
        non_aligned_actully_total_transferred_size += transferred;
        acctually_total_transferred_size += transferred;
        // delay a bit if get nothing from interface driver
        if (transferred == 0) {
            platform_usleep(100);
        }
    }

    if (acctually_total_transferred_size > 0) {
        // update pointer only if data has been transferred successfully
        int32_t nume_of_entry_by_hight_transferred = (int32_t)(acctually_total_transferred_size / size_of_one_entry_by_row); // actual entry number
        platform_mutex_lock(&ofmap_ringbuffer->Cascade.write_guard);
        for (int32_t update_entry_count = 0 ;update_entry_count < nume_of_entry_by_hight_transferred; update_entry_count++) {
            ofmap_ringbuffer->Cascade.entry_w = ((ofmap_ringbuffer->Cascade.entry_w + 1) == max_num_of_entry_by_hight) ? 0 : ofmap_ringbuffer->Cascade.entry_w + 1;
        }
        platform_mutex_unlock(&ofmap_ringbuffer->Cascade.write_guard);
        // enqueue ring-buffer to queue job if there is complete frame data available
        int32_t nume_of_entry_need_updated = (ofmap_ringbuffer->Cascade.entry_w >= ofmap_ringbuffer->Cascade.entry_report_r) ?
                                          (ofmap_ringbuffer->Cascade.entry_w - ofmap_ringbuffer->Cascade.entry_report_r) :
                                          (max_num_of_entry_by_hight - (ofmap_ringbuffer->Cascade.entry_report_r - ofmap_ringbuffer->Cascade.entry_w));

        // maybe enqueue multiple frames at once
        int32_t num_of_frame_need_to_enqueue = nume_of_entry_need_updated / ofmap_info->Cascade.fmap.height;
        while (num_of_frame_need_to_enqueue--) {
            status = _memx_cascade_ofmap_job_enqueue(mpu, flow_id, (job_action_t)&_memx_cascade_job_ofmap_decode);
            if (memx_status_error(status)) {
                printf("ofmap_job_enqueue fail at line(%d), status(%d)\n", __LINE__, status);
                break;
            } else {
                ofmap_ringbuffer->Cascade.entry_report_r = ((ofmap_ringbuffer->Cascade.entry_report_r + ofmap_info->Cascade.fmap.height) == ofmap_ringbuffer->Cascade.max_num_of_entry_by_hight) ?
                                                                                                0 : ofmap_ringbuffer->Cascade.entry_report_r + ofmap_info->Cascade.fmap.height;
            }
        }
    }
    return acctually_total_transferred_size;
}

/**
 * @brief Background worker helps to poll data out from interface driver and
 * send to corresponding flow's ring-buffer.
 *
 * @param user_data           ofmap worker control information
 *
 * @return none
 */
void *_memx_cascade_ofmap_worker_dowork(void *user_data)
{
    if (!user_data) { printf("ofmap_worker_dowork: user_data is NULL\n"); return NULL; }
    MemxMpuWorkerInfo *ofmap_worker_info = (MemxMpuWorkerInfo *)user_data;
    if (!ofmap_worker_info->mpu || !ofmap_worker_info->mpu->context) { printf("ofmap_worker_dowork: mpu or context is NULL\n"); return NULL; }
    MemxCascade *cascade = (MemxCascade *)ofmap_worker_info->mpu->context;

    // infinite loop, stops only when program is terminated
    while (ofmap_worker_info->enable == 1) {
        // go to pending on demand
        _memx_mpu_worker_go_to_sleep(ofmap_worker_info);

        // do round-robin polling to check if any ofmap ring-buffer has space
        size_t is_any_data_transferred = 0;
        for (uint8_t flow_id = 0; flow_id  < _CASCADE_MPU_OFMAP_FLOW_NUMBER; ++flow_id) {
            MemxMpuFmapRingBuffer *ofmap_ringbuffer = &cascade->ofmap_ringbuffers[flow_id];
            // ask for data only if ring-buffer has space
            if (((ofmap_ringbuffer->Cascade.valid == 1) &&
                ((ofmap_ringbuffer->Cascade.entry_w + 1) % ofmap_ringbuffer->Cascade.max_num_of_entry_by_hight != ofmap_ringbuffer->Cascade.entry_r))) { // not full
                is_any_data_transferred += _memx_cascade_ofmap_worker_transfer(ofmap_worker_info, flow_id)  == 0 ? 0 : 1;
            }
        }
        // cannot go to sleep since we can obtain ofmap only by polling
        // simply add some delay to thread if no data transferred
        if (is_any_data_transferred == 0) {
            platform_usleep(100);
        } // TODO: 1ms or 0.1ms delay is better?
    }

    return NULL;
}

/**
 * @brief Runtime input feature map information update. Based on Cascade's
 * design, ifmap should always be row and frame with 4 bytes alignment.
 * Frame-alignment here is hardware limitation while row-alignment is software
 * limitation for we transfer data to device by row to reduce latency.
 *
 * @param mpu                 MPU context
 * @param flow_id             target flow ID
 * @param height              input feature map height
 * @param width               input feature map width
 * @param z                   input feature map z
 * @param channel_number      input feature map channel number
 * @param format              input feature map format
 *
 * @return 0 on success, otherwise error code
 */
memx_status _memx_cacasde_ifmap_info_update(MemxMpu *mpu, uint8_t flow_id, int32_t height, int32_t width, int32_t z, int32_t channel_number, int32_t format)
{
    if (!mpu) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    if (!mpu->context) { return MEMX_STATUS_MPU_INVALID_IO_CONTEXT; }
    if (flow_id >= _CASCADE_MPU_IFMAP_FLOW_NUMBER) { return MEMX_STATUS_MPU_INVALID_FLOW_ID; }
    if (height < 0 || width < 0 || z != 1 || channel_number < 0 || format < 0) { return MEMX_STATUS_MPU_INVALID_PARAMETER; } // cascade only support z=1

    MemxCascade *cascade = (MemxCascade *)mpu->context;;
    MemxMpuIfmapInfo *ifmap_info = &cascade->ifmap_infos[flow_id];

    // update new input feature map size
    switch (format) {
    case MEMX_MPU_IFMAP_FORMAT_FLOAT32: {
        ifmap_info->fmap.format = format;
        ifmap_info->fmap.channel_number = channel_number;
        ifmap_info->fmap.z = z;
        ifmap_info->fmap.width = width;
        ifmap_info->fmap.height = height;
        ifmap_info->fmap.channel_size = sizeof(float);
        ifmap_info->fmap.row_size = width * channel_number;
        ifmap_info->fmap.user_frame_size = height * ifmap_info->fmap.row_size;
        memx_gbf_get_gbf80_channel_number_reshaped(&ifmap_info->fmap.fmap_channel_number, channel_number);
        ifmap_info->fmap.fmap_pixel_size = (ifmap_info->fmap.fmap_channel_number >> 3) * 10;
        memx_gbf_get_gbf80_row_size_reshaped(&ifmap_info->fmap.fmap_row_size, width, z, channel_number);
        memx_gbf_get_gbf80_frame_size_reshaped(&ifmap_info->fmap.fmap_frame_size, height, width, z, channel_number);
    } break;
    case MEMX_MPU_IFMAP_FORMAT_GBF80: {
        ifmap_info->fmap.format = format;
        ifmap_info->fmap.channel_number = channel_number;
        ifmap_info->fmap.z = z;
        ifmap_info->fmap.width = width;
        ifmap_info->fmap.height = height;
        ifmap_info->fmap.channel_size = sizeof(uint8_t);
        ifmap_info->fmap.row_size = width * ((channel_number+7) >> 3) * 10;
        ifmap_info->fmap.user_frame_size = height * ifmap_info->fmap.row_size;
        memx_gbf_get_gbf80_channel_number_reshaped(&ifmap_info->fmap.fmap_channel_number, channel_number);
        ifmap_info->fmap.fmap_pixel_size = (ifmap_info->fmap.fmap_channel_number >> 3) * 10;
        memx_gbf_get_gbf80_row_size_reshaped(&ifmap_info->fmap.fmap_row_size, width, z, channel_number);
        memx_gbf_get_gbf80_frame_size_reshaped(&ifmap_info->fmap.fmap_frame_size, height, width, z, channel_number);
    } break;
    default: { // MEMX_MPU_IFMAP_FORMAT_RAW
        // using frame size as row size for MEMX_MPU_IFMAP_FORMAT_RAW
        width = height * width;
        height = 1;
        ifmap_info->fmap.format = format;
        ifmap_info->fmap.channel_number = channel_number;
        ifmap_info->fmap.z = z;
        ifmap_info->fmap.width = width;
        ifmap_info->fmap.height = height;
        ifmap_info->fmap.channel_size = sizeof(uint8_t);
        ifmap_info->fmap.row_size = width * channel_number;
        ifmap_info->fmap.user_frame_size = height * ifmap_info->fmap.row_size;
        ifmap_info->fmap.fmap_channel_number = channel_number;
        ifmap_info->fmap.fmap_pixel_size = channel_number;
        ifmap_info->fmap.fmap_row_size = (width * ifmap_info->fmap.fmap_channel_number + 3) & ~0x3; // row padding to 4 bytes
        ifmap_info->fmap.fmap_frame_size = (height * ifmap_info->fmap.fmap_row_size + 3) & ~0x3; // frame padding to 4 bytes
    }
    }
    // input floating-point to rgb888 range conversion
    ifmap_info->range_convert_shift = 0;
    ifmap_info->range_convert_scale = 0;

    return MEMX_STATUS_OK;
}

/**
 * @brief Re-configure input feature map with floating-point to RGB888 data
 * range conversion enabled. Input feature map buffer size will be re-shaped,
 * in case internal ring-buffer is also required to be updated.
 *
 * @param mpu                 MPU context
 * @param flow_id             target flow ID
 * @param shift               amount to shift before scale
 * @param scale               amount to scale before integer cast
 *
 * @return 0 on success, otherwise error code
 */
memx_status _memx_cascade_ifmap_info_update_range_convert(MemxMpu *mpu, uint8_t flow_id, float shift, float scale)
{
    if (!mpu) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    if (!mpu->context) { return MEMX_STATUS_MPU_INVALID_IO_CONTEXT; }
    if (flow_id >= _CASCADE_MPU_IFMAP_FLOW_NUMBER) { return MEMX_STATUS_MPU_INVALID_FLOW_ID; }
    MemxCascade *cascade = (MemxCascade *)mpu->context;;
    MemxMpuIfmapInfo *ifmap_info = &cascade->ifmap_infos[flow_id];

    // follows configuration in MEMX_MPU_IFMAP_FORMAT_RAW
    ifmap_info->fmap.format = MEMX_MPU_IFMAP_FORMAT_FLOAT32_RGB888;
    ifmap_info->fmap.channel_number = ifmap_info->fmap.channel_number;
    ifmap_info->fmap.width = ifmap_info->fmap.width;
    ifmap_info->fmap.height = ifmap_info->fmap.height;
    ifmap_info->fmap.channel_size = sizeof(float); // user input format will be float32
    ifmap_info->fmap.row_size = ifmap_info->fmap.width * ifmap_info->fmap.channel_number;
    ifmap_info->fmap.user_frame_size = ifmap_info->fmap.height * ifmap_info->fmap.row_size;
    ifmap_info->fmap.fmap_channel_number = ifmap_info->fmap.channel_number;
    ifmap_info->fmap.fmap_pixel_size = ifmap_info->fmap.channel_number;
    ifmap_info->fmap.fmap_row_size = (ifmap_info->fmap.width * ifmap_info->fmap.fmap_channel_number + 3) & ~0x3;
    ifmap_info->fmap.fmap_frame_size = (ifmap_info->fmap.height * ifmap_info->fmap.fmap_row_size + 3) & ~0x3;
    ifmap_info->range_convert_shift = shift;
    ifmap_info->range_convert_scale = scale;

    return MEMX_STATUS_OK;
}

/**
 * @brief Runtime input feature map ring-buffer update.
 *
 * @param mpu                 MPU context
 * @param flow_id             target flow ID
 *
 * @return 0 on success, otherwise error code
 */
memx_status _memx_cascade_ifmap_ringbuffer_update(MemxMpu *mpu, uint8_t flow_id)
{
    if (!mpu) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    if (!mpu->context) { return MEMX_STATUS_MPU_INVALID_IO_CONTEXT; }

    MemxCascade *cascade = (MemxCascade *)mpu->context;
    MemxMpuIfmapInfo *ifmap_info = &cascade->ifmap_infos[flow_id];
    if (ifmap_info->fmap.user_frame_size <= 0) { return MEMX_STATUS_MPU_INVALID_DATALEN; }

    uint8_t *new_data_ring_buffer = (uint8_t *)malloc(ifmap_info->fmap.fmap_frame_size * sizeof(uint8_t) * _CASCADE_MPU_IFMAP_RINGBUFFER_FRAME_NUMBER);
    if (!new_data_ring_buffer) { return MEMX_STATUS_MPU_ALLOCATE_IFMAP_BUFFER_FAIL; }

    MemxMpuFmapRingBuffer *ifmap_ringbuffer = &cascade->ifmap_ringbuffers[flow_id];
    // update new ring-buffer size, but do not enable flow immediately
    ifmap_ringbuffer->Cascade.max_num_of_entry_by_hight = ifmap_info->fmap.height * _CASCADE_MPU_IFMAP_RINGBUFFER_FRAME_NUMBER;
    ifmap_ringbuffer->Cascade.size_of_one_entry_by_row = ifmap_info->fmap.fmap_row_size;
    ifmap_ringbuffer->Cascade.entry_r = 0;
    ifmap_ringbuffer->Cascade.entry_w = 0;

    // re-allocate buffer memory space
    free(ifmap_ringbuffer->Cascade.data);
    ifmap_ringbuffer->Cascade.data = NULL;
    ifmap_ringbuffer->Cascade.data = new_data_ring_buffer;

    return MEMX_STATUS_OK;
}

/**
 * @brief Runtime output feature map information update. Based on Cascade's
 * design, ofmap should always be row and frame with 4 bytes alignment. Both
 * frame-alignment and row-alignment are hardware limitations.
 *
 * @param mpu                 MPU context
 * @param flow_id             target flow ID
 * @param height              output feature map height
 * @param width               output feature map width
 * @param z                   output feature map z
 * @param channel_number      output feature map channel number
 * @param format              output feature map format
 *
 * @return 0 on success, otherwise error code
 */
memx_status _memx_cacasde_ofmap_info_update(MemxMpu *mpu, uint8_t flow_id, int32_t height, int32_t width, int32_t z, int32_t channel_number, int32_t format)
{
    if (!mpu) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    if (!mpu->context) { return MEMX_STATUS_MPU_INVALID_IO_CONTEXT; }
    if (flow_id >= _CASCADE_MPU_OFMAP_FLOW_NUMBER) { return MEMX_STATUS_MPU_INVALID_FLOW_ID; }
    if (height <= 0 || width <= 0 || z != 1 || channel_number <= 0 || format <= 0) { return MEMX_STATUS_MPU_INVALID_PARAMETER; } // cascade only support z=1

    MemxCascade *cascade = (MemxCascade *)mpu->context;
    MemxMpuOfmapInfo *ofmap_info = &cascade->ofmap_infos[flow_id];
    // update new input feature map size
    switch (format) {
    case MEMX_MPU_OFMAP_FORMAT_FLOAT32 : {
        ofmap_info->Cascade.fmap.format = format;
        ofmap_info->Cascade.fmap.channel_number = channel_number;
        ofmap_info->Cascade.fmap.z = z;
        ofmap_info->Cascade.fmap.width = width;
        ofmap_info->Cascade.fmap.height = height;
        ofmap_info->Cascade.fmap.channel_size = sizeof(float);
        ofmap_info->Cascade.fmap.row_size = width * channel_number;
        ofmap_info->Cascade.fmap.user_frame_size = height * ofmap_info->Cascade.fmap.row_size;
        memx_gbf_get_gbf80_channel_number_reshaped(&ofmap_info->Cascade.fmap.fmap_channel_number, channel_number);
        ofmap_info->Cascade.fmap.fmap_pixel_size = (ofmap_info->Cascade.fmap.fmap_channel_number >> 3) * 10;
        memx_gbf_get_gbf80_row_size_reshaped(&ofmap_info->Cascade.fmap.fmap_row_size, width, z, channel_number);
        memx_gbf_get_gbf80_frame_size_reshaped(&ofmap_info->Cascade.fmap.fmap_frame_size, height, width, z, channel_number);
    } break;
    case MEMX_MPU_OFMAP_FORMAT_GBF80 : {
        ofmap_info->Cascade.fmap.format = format;
        ofmap_info->Cascade.fmap.channel_number = channel_number;
        ofmap_info->Cascade.fmap.z = z;
        ofmap_info->Cascade.fmap.width = width;
        ofmap_info->Cascade.fmap.height = height;
        ofmap_info->Cascade.fmap.channel_size = sizeof(uint8_t);
        ofmap_info->Cascade.fmap.row_size = width * ((channel_number+7) >> 3) * 10;
        ofmap_info->Cascade.fmap.user_frame_size = height * ofmap_info->Cascade.fmap.row_size;
        memx_gbf_get_gbf80_channel_number_reshaped(&ofmap_info->Cascade.fmap.fmap_channel_number, channel_number);
        ofmap_info->Cascade.fmap.fmap_pixel_size = (ofmap_info->Cascade.fmap.fmap_channel_number >> 3) * 10;
        memx_gbf_get_gbf80_row_size_reshaped(&ofmap_info->Cascade.fmap.fmap_row_size, width, z, channel_number);
        memx_gbf_get_gbf80_frame_size_reshaped(&ofmap_info->Cascade.fmap.fmap_frame_size, height, width, z, channel_number);
    } break;
    default: { // MEMX_MPU_OFMAP_FORMAT_RAW
        ofmap_info->Cascade.fmap.format = format;
        ofmap_info->Cascade.fmap.channel_number = channel_number;
        ofmap_info->Cascade.fmap.z = z;
        ofmap_info->Cascade.fmap.width = width;
        ofmap_info->Cascade.fmap.height = height;
        ofmap_info->Cascade.fmap.channel_size = sizeof(uint8_t);
        ofmap_info->Cascade.fmap.row_size = width * channel_number;
        ofmap_info->Cascade.fmap.user_frame_size = height * ofmap_info->Cascade.fmap.row_size;
        ofmap_info->Cascade.fmap.fmap_channel_number = channel_number;
        ofmap_info->Cascade.fmap.fmap_pixel_size = channel_number;
        ofmap_info->Cascade.fmap.fmap_row_size = (width * ofmap_info->Cascade.fmap.fmap_channel_number + 3) & ~0x3; // row padding to 4 bytes
        ofmap_info->Cascade.fmap.fmap_frame_size = (height * ofmap_info->Cascade.fmap.fmap_row_size + 3) & ~0x3; // frame padding to 4 bytes
    }
    }

    // high-precision-output-channel
    if (ofmap_info->Cascade.hpoc_dummy_channel_indexes != NULL) {
        free(ofmap_info->Cascade.hpoc_dummy_channel_indexes);
        ofmap_info->Cascade.hpoc_dummy_channel_indexes = NULL;
        ofmap_info->Cascade.hpoc_dummy_channel_number = 0;
        ofmap_info->Cascade.hpoc_dummy_channel_array_size = 0;
    }

    return MEMX_STATUS_OK;
}

/**
 * @brief Re-configure output feature map with HPOC (High-Precision Output
 * Channel) setting. Each HPOC occupy one GBF80 entry and be placed at the 1st
 * channel within this entry, which means dummy channels are inserted before and
 * after to make previous GBF80 and this GBF80 complete with 8 channels.
 *
 * @param mpu                 MPU context
 * @param flow_id             target flow ID
 * @param hpoc_size           high-precision-output-channel number
 * @param hpoc_indexes        high-precision-output-channel indexes
 *
 * @return 0 on success, otherwise error code
 */
memx_status _memx_cascade_ofmap_info_update_hpoc(MemxMpu *mpu, uint8_t flow_id, int32_t hpoc_size, int32_t *hpoc_indexes)
{
  memx_status status = MEMX_STATUS_OK;
  MemxCascade* cascade = (MemxCascade*)mpu->context;;
  MemxMpuOfmapInfo* ofmap_info = &cascade->ofmap_infos[flow_id];

    // update output feature map setting

    ofmap_info->Cascade.fmap.format = MEMX_MPU_OFMAP_FORMAT_FLOAT32_HPOC;

    // to_user_ofmap should not include hpoc dummy channel.
    ofmap_info->Cascade.fmap.user_frame_size = ofmap_info->Cascade.fmap.height * ofmap_info->Cascade.fmap.row_size;

    // internal_ofmap will include hpoc dummy channel
    ofmap_info->Cascade.fmap.channel_number = ofmap_info->Cascade.fmap.channel_number + hpoc_size;
    ofmap_info->Cascade.hpoc_dummy_channel_number = ofmap_info->Cascade.fmap.channel_number;
    ofmap_info->Cascade.fmap.width = ofmap_info->Cascade.fmap.width;
    ofmap_info->Cascade.fmap.height = ofmap_info->Cascade.fmap.height;
    ofmap_info->Cascade.fmap.z = ofmap_info->Cascade.fmap.z;
    ofmap_info->Cascade.fmap.channel_size = sizeof(float);
    ofmap_info->Cascade.fmap.row_size = ofmap_info->Cascade.fmap.width * ofmap_info->Cascade.fmap.channel_number;

    memx_gbf_get_gbf80_channel_number_reshaped(&ofmap_info->Cascade.fmap.fmap_channel_number, ofmap_info->Cascade.fmap.channel_number);
    ofmap_info->Cascade.fmap.fmap_pixel_size = (ofmap_info->Cascade.fmap.fmap_channel_number >> 3) * 10;
    memx_gbf_get_gbf80_row_size_reshaped(&ofmap_info->Cascade.fmap.fmap_row_size, ofmap_info->Cascade.fmap.width, ofmap_info->Cascade.fmap.z, ofmap_info->Cascade.fmap.channel_number);
    memx_gbf_get_gbf80_frame_size_reshaped(&ofmap_info->Cascade.fmap.fmap_frame_size, ofmap_info->Cascade.fmap.height, ofmap_info->Cascade.fmap.width, ofmap_info->Cascade.fmap.z, ofmap_info->Cascade.fmap.channel_number);



    ofmap_info->Cascade.hpoc_dummy_channel_indexes = (int32_t*)malloc(sizeof(int32_t) * hpoc_size);
    if (!ofmap_info->Cascade.hpoc_dummy_channel_indexes) { return MEMX_STATUS_MPU_HPOC_CONFIG_FAIL; }
    memcpy(ofmap_info->Cascade.hpoc_dummy_channel_indexes, hpoc_indexes, sizeof(int32_t) * hpoc_size);
    ofmap_info->Cascade.hpoc_dummy_channel_array_size = hpoc_size;

    return status;
}

/**
 * @brief Runtime output feature map ring-buffer update.
 *
 * @param mpu                 MPU context
 * @param flow_id             target flow ID
 *
 * @return 0 on success, otherwise error code
 */
memx_status _memx_cascade_ofmap_ringbuffer_update(MemxMpu *mpu, uint8_t flow_id)
{
    if (!mpu) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    if (!mpu->context) { return MEMX_STATUS_MPU_INVALID_IO_CONTEXT; }
    if (flow_id >= _CASCADE_MPU_OFMAP_FLOW_NUMBER) { return MEMX_STATUS_MPU_INVALID_FLOW_ID; }
    MemxCascade *cascade = (MemxCascade *)mpu->context;
    MemxMpuOfmapInfo *ofmap_info = &cascade->ofmap_infos[flow_id];
    if (ofmap_info->Cascade.fmap.user_frame_size <= 0) { return MEMX_STATUS_MPU_INVALID_DATALEN; }

    uint8_t *new_data_ring_buffer = (uint8_t *)malloc(ofmap_info->Cascade.fmap.fmap_frame_size * sizeof(uint8_t) * _CASCADE_MPU_OFMAP_RINGBUFFER_FRAME_NUMBER);
    if (!new_data_ring_buffer) { return MEMX_STATUS_MPU_ALLOCATE_IFMAP_BUFFER_FAIL; }

    MemxMpuFmapRingBuffer *ofmap_ringbuffer = &cascade->ofmap_ringbuffers[flow_id];

    // update new ring-buffer size, but do not enable flow immediately
    ofmap_ringbuffer->Cascade.max_num_of_entry_by_hight = ofmap_info->Cascade.fmap.height *_CASCADE_MPU_OFMAP_RINGBUFFER_FRAME_NUMBER;
    ofmap_ringbuffer->Cascade.size_of_one_entry_by_row = ofmap_info->Cascade.fmap.fmap_row_size;
    ofmap_ringbuffer->Cascade.entry_r = 0;
    ofmap_ringbuffer->Cascade.entry_w = 0;
    ofmap_ringbuffer->Cascade.entry_report_r = 0;

    // re-allocate buffer memory space
    free(ofmap_ringbuffer->Cascade.data);
    ofmap_ringbuffer->Cascade.data = NULL;
    ofmap_ringbuffer->Cascade.data = new_data_ring_buffer;
    return MEMX_STATUS_OK;
}

/***************************************************************************//**
 * implementation
 ******************************************************************************/
MemxMpu* memx_cascade_create(MemxMpu* mpu, uint8_t mpu_group_id)
{
  MemxCascade* cascade = NULL;
  if (mpu == NULL) {
    goto end;
  }

  cascade = (MemxCascade*)malloc(sizeof(MemxCascade));
  if(cascade == NULL)
    goto fail;
  memset(cascade, 0, sizeof(MemxCascade));

  // create input feature map queue, ring-buffer and on-the-air counter
  cascade->ifmap_queue_size = _CASCADE_MPU_IFMAP_QUEUE_DEFAULT_FRAME_NUMBER;

  for(int32_t i=0; i<_CASCADE_MPU_IFMAP_FLOW_NUMBER; ++i) {
    cascade->ifmap_infos[i].valid = 0;
    cascade->ifmap_infos[i].range_convert_scale = 0;
    cascade->ifmap_infos[i].range_convert_shift = 0;
    cascade->ifmap_queues[i] = memx_list_create();
    cascade->ifmap_ringbuffers[i].Cascade.valid = 0;
    cascade->ifmap_ringbuffers[i].Cascade.data = NULL;
    if((cascade->ifmap_queues[i] == NULL)
      ||platform_cond_init(&cascade->ifmap_ringbuffers[i].Cascade.pointer_changed, NULL)
      ||platform_mutex_create(&cascade->ifmap_ringbuffers[i].Cascade.guard, NULL)
      ||platform_mutex_create(&cascade->ifmap_ringbuffers[i].Cascade.read_guard, NULL)
      ||platform_mutex_create(&cascade->ifmap_ringbuffers[i].Cascade.write_guard, NULL))
      goto fail;
  }

  cascade->ifmap_ota_entry_count.value = 0;
  if(platform_cond_init(&cascade->ifmap_ota_entry_count.value_changed, NULL)
    ||platform_mutex_create(&cascade->ifmap_ota_entry_count.guard, NULL))
    goto fail;

  // create output feature map queue, ring-buffer and on-the-air counter
  cascade->ofmap_queue_size = _CASCADE_MPU_OFMAP_QUEUE_DEFAULT_FRAME_NUMBER;
  for(int32_t i=0; i<_CASCADE_MPU_OFMAP_FLOW_NUMBER; ++i) {
    cascade->ofmap_infos[i].Cascade.valid = 0;
    cascade->ofmap_infos[i].Cascade.hpoc_dummy_channel_number = 0;
    cascade->ofmap_infos[i].Cascade.hpoc_dummy_channel_indexes = NULL;
    cascade->ofmap_infos[i].Cascade.hpoc_dummy_channel_array_size = 0;
    cascade->ofmap_queues[i] = memx_list_create();
    cascade->ofmap_ringbuffers[i].Cascade.valid = 0;
    cascade->ofmap_ringbuffers[i].Cascade.data = NULL;

    if((cascade->ofmap_queues[i] == NULL)
      ||platform_cond_init(&cascade->ofmap_ringbuffers[i].Cascade.pointer_changed, NULL)
      ||platform_mutex_create(&cascade->ofmap_ringbuffers[i].Cascade.guard, NULL)
      ||platform_mutex_create(&cascade->ofmap_ringbuffers[i].Cascade.read_guard, NULL)
      ||platform_mutex_create(&cascade->ofmap_ringbuffers[i].Cascade.write_guard, NULL))
      goto fail;
  }

  // create job queue
  cascade->ifmap_job_queue.queue = memx_list_create();
  if((cascade->ifmap_job_queue.queue == NULL)
    ||platform_cond_init(&cascade->ifmap_job_queue.enqueue, NULL)
    ||platform_mutex_create(&cascade->ifmap_job_queue.guard, NULL))
    goto fail;

  cascade->ofmap_job_queue.queue = memx_list_create();
  if((cascade->ofmap_job_queue.queue == NULL)
    ||platform_cond_init(&cascade->ofmap_job_queue.enqueue, NULL)
    ||platform_mutex_create(&cascade->ofmap_job_queue.guard, NULL))
    goto fail;
  // setup callback
  mpu->context = (_memx_mpu_context_t)cascade;
  mpu->destroy = (_memx_mpu_destroy_cb)&memx_cascade_destroy;
  mpu->reconfigure = (_memx_mpu_reconfigure_cb)&memx_cascade_reconfigure;
  mpu->operation = (_memx_mpu_operation_cb)&memx_cascade_operation;
  mpu->control_write = NULL;
  mpu->control_read = NULL;
  mpu->stream_write = NULL;
  mpu->stream_read = NULL;
  mpu->stream_ifmap = (_memx_mpu_stream_ifmap_cb)&memx_cascade_stream_ifmap;
  mpu->stream_ofmap = (_memx_mpu_stream_ofmap_cb)&memx_cascade_stream_ofmap;
  mpu->download_model = (_memx_mpu_download_model_cb)&memx_cascade_download_model;
  mpu->download_firmware = (_memx_mpu_download_firmware_cb)&memx_cascade_download_firmware;
  mpu->set_worker_number = (_memx_mpu_set_worker_number_cb)&memx_cascade_set_worker_number;
  mpu->set_stream_enable = (_memx_mpu_set_stream_enable_cb)&memx_cascade_set_stream_enable;
  mpu->set_stream_disable = (_memx_mpu_set_stream_disable_cb)&memx_cascade_set_stream_disable;
  mpu->set_ifmap_queue_size = (_memx_mpu_set_ifmap_queue_size_cb)&memx_cascade_set_ifmap_queue_size;
  mpu->set_ifmap_size = (_memx_mpu_set_ifmap_size_cb)&memx_cascade_set_ifmap_size;
  mpu->set_ifmap_range_convert = (_memx_mpu_set_ifmap_range_convert_cb)&memx_cascade_set_ifmap_range_convert;
  mpu->set_ofmap_queue_size = (_memx_mpu_set_ofmap_queue_size_cb)&memx_cascade_set_ofmap_queue_size;
  mpu->set_ofmap_size = (_memx_mpu_set_ofmap_size_cb)&memx_cascade_set_ofmap_size;
  mpu->set_ofmap_hpoc = (_memx_mpu_set_ofmap_hpoc_cb)&memx_cascade_set_ofmap_hpoc;
  mpu->update_fmap_size = (_memx_mpu_update_fmap_size_cb)&memx_cascade_update_fmap_size;
  mpu->max_input_flow_cnt = _CASCADE_MPU_IFMAP_FLOW_NUMBER;
  mpu->max_output_flow_cnt = _CASCADE_MPU_OFMAP_FLOW_NUMBER;
  mpu->chip_gen = MEMX_MPU_CHIP_GEN_CASCADE;
  mpu->input_chip_id = 0; // input_chip_id fixed to 0 for cascade
  mpu->output_chip_id = 0; // output_chip_id fixed to 0 for cascade
  mpu->mpu_group_id = mpu_group_id;
  mpu->input_mode_flag = 0;

  // start ifmap worker, default on
  cascade->ifmap_worker_info.enable = 1;
  cascade->ifmap_worker_info.mpu = mpu;
  cascade->ifmap_worker_info.state = _mpu_worker_state_pending;
  cascade->ifmap_worker_info.state_next = _mpu_worker_state_pending;
  if(platform_cond_init(&cascade->ifmap_worker_info.state_changed, NULL)
    ||platform_mutex_create(&cascade->ifmap_worker_info.guard, NULL)
    ||platform_thread_create(&cascade->ifmap_worker_info.worker, NULL, &_memx_cascade_ifmap_worker_dowork, &cascade->ifmap_worker_info))
    goto fail;
  // start ofmap worker, default on
  cascade->ofmap_worker_info.enable = 1;
  cascade->ofmap_worker_info.mpu = mpu;
  cascade->ofmap_worker_info.state = _mpu_worker_state_pending;
  cascade->ofmap_worker_info.state_next = _mpu_worker_state_pending;
  if(platform_cond_init(&cascade->ofmap_worker_info.state_changed, NULL)
    ||platform_mutex_create(&cascade->ofmap_worker_info.guard, NULL)
    ||platform_thread_create(&cascade->ofmap_worker_info.worker, NULL, &_memx_cascade_ofmap_worker_dowork, &cascade->ofmap_worker_info))
    goto fail;

#if _WIN32
  SetThreadPriority(&cascade->ofmap_worker_info.worker, THREAD_PRIORITY_ABOVE_NORMAL);
#endif

  // start job workers, use default number and default to running
  // since job worker only move data between internal buffers, just let it go to sleep automatically
  cascade->job_worker_number = _CASCADE_MPU_JOB_WORKER_DEFAULT_NUMBER;
  cascade->ifmap_job_worker_infos = memx_list_create();
  cascade->ofmap_job_worker_infos = memx_list_create();

  if(cascade->ifmap_job_worker_infos == NULL || cascade->ofmap_job_worker_infos == NULL)
    goto fail;

  // create job workers for ifmap job queue
  for(int32_t i=0; i < cascade->job_worker_number; ++i) {
    MemxMpuWorkerInfo* job_worker_info = (MemxMpuWorkerInfo*)malloc(sizeof(MemxMpuWorkerInfo));
    if(job_worker_info == NULL)
      goto fail;
    job_worker_info->enable = 1;
    job_worker_info->mpu = mpu;
    job_worker_info->state = _mpu_worker_state_running;
    job_worker_info->state_next = _mpu_worker_state_running;
    if(platform_cond_init(&job_worker_info->state_changed, NULL)
      ||platform_mutex_create(&job_worker_info->guard, NULL)
      ||platform_thread_create(&job_worker_info->worker, NULL, &_memx_cascade_ifmap_job_worker_dowork, job_worker_info)
      ||(memx_list_push(cascade->ifmap_job_worker_infos, job_worker_info) != 0))
      goto fail;
  }

  // create job workers for ofmap job queue
  for(int32_t i=0; i < cascade->job_worker_number; ++i) {
    MemxMpuWorkerInfo* job_worker_info = (MemxMpuWorkerInfo*)malloc(sizeof(MemxMpuWorkerInfo));
    if(job_worker_info == NULL)
      goto fail;
    job_worker_info->enable = 1;
    job_worker_info->mpu = mpu;
    job_worker_info->state = _mpu_worker_state_running;
    job_worker_info->state_next = _mpu_worker_state_running;
    if(platform_cond_init(&job_worker_info->state_changed, NULL)
      ||platform_mutex_create(&job_worker_info->guard, NULL)
      ||platform_thread_create(&job_worker_info->worker, NULL, &_memx_cascade_ofmap_job_worker_dowork, job_worker_info)
      ||(memx_list_push(cascade->ofmap_job_worker_infos, job_worker_info) != 0))
      goto fail;
  }

end:
  return mpu;

fail:
    if (cascade != NULL) {
        free(cascade);
        cascade = NULL;
    }
    memx_mpu_destroy(mpu);
    return NULL;
}

void memx_cascade_destroy(MemxMpu *mpu)
{
    if (!mpu || !mpu->context) { return; }
    MemxCascade *cascade = (MemxCascade *)mpu->context;

    // stop all workers
    cascade->ifmap_worker_info.enable = 0;
    cascade->ofmap_worker_info.enable = 0;
    for (int32_t idx = 0; idx < memx_list_count(cascade->ifmap_job_worker_infos); ++idx) {
        MemxMpuWorkerInfo *ifmap_job_worker_info = (MemxMpuWorkerInfo *)memx_list_peek(cascade->ifmap_job_worker_infos, idx);
        ifmap_job_worker_info->enable = 0;
    }
    for (int32_t idx = 0; idx < memx_list_count(cascade->ofmap_job_worker_infos); ++idx) {
        MemxMpuWorkerInfo *ofmap_job_worker_info = (MemxMpuWorkerInfo *)memx_list_peek(cascade->ofmap_job_worker_infos, idx);
        ofmap_job_worker_info->enable = 0;
    }
    // signal to wake-up workers and go to exit
    // no need to care about which state since we already set enable to '0'
    _memx_mpu_worker_go_to_state(&cascade->ifmap_worker_info, _mpu_worker_state_pending);
    _memx_mpu_worker_go_to_state(&cascade->ofmap_worker_info, _mpu_worker_state_pending);
    pthread_cond_signal_with_lock(&cascade->ifmap_ota_entry_count.value_changed, &cascade->ifmap_ota_entry_count.guard);

    for (int32_t i = 0; i < memx_list_count(cascade->ifmap_job_worker_infos); ++i) {
        MemxMpuWorkerInfo *job_worker_info = (MemxMpuWorkerInfo *)memx_list_peek(cascade->ifmap_job_worker_infos, i);
        _memx_mpu_worker_go_to_state(job_worker_info, _mpu_worker_state_pending);
        pthread_cond_signal_with_lock(&cascade->ifmap_job_queue.enqueue, &cascade->ifmap_job_queue.guard);
    }
    for (int32_t i = 0; i < memx_list_count(cascade->ofmap_job_worker_infos); ++i) {
        MemxMpuWorkerInfo *job_worker_info = (MemxMpuWorkerInfo *)memx_list_peek(cascade->ofmap_job_worker_infos, i);
        _memx_mpu_worker_go_to_state(job_worker_info, _mpu_worker_state_pending);
        pthread_cond_signal_with_lock(&cascade->ofmap_job_queue.enqueue, &cascade->ofmap_job_queue.guard);
    }

    // join all workers
    platform_thread_join(&cascade->ifmap_worker_info.worker, NULL);
    platform_thread_join(&cascade->ofmap_worker_info.worker, NULL);

    while (memx_list_count(cascade->ifmap_job_worker_infos) > 0) {
        MemxMpuWorkerInfo *job_worker_info = (MemxMpuWorkerInfo *)memx_list_pop(cascade->ifmap_job_worker_infos);
        platform_thread_join(&job_worker_info->worker, NULL);
        platform_mutex_destory(&job_worker_info->guard);
        free(job_worker_info);
        job_worker_info = NULL;
    }
    platform_mutex_destory(&cascade->ifmap_worker_info.guard);
    memx_list_destroy(cascade->ifmap_job_worker_infos);

    while (memx_list_count(cascade->ofmap_job_worker_infos) > 0) {
        MemxMpuWorkerInfo *job_worker_info = (MemxMpuWorkerInfo *)memx_list_pop(cascade->ofmap_job_worker_infos);
        platform_thread_join(&job_worker_info->worker, NULL);
        platform_mutex_destory(&job_worker_info->guard);
        free(job_worker_info);
        job_worker_info = NULL;
    }
    platform_mutex_destory(&cascade->ofmap_worker_info.guard);
    memx_list_destroy(cascade->ofmap_job_worker_infos);
    // clean-up input feature maps in queue
    for (int32_t flow_idx = 0; flow_idx < _CASCADE_MPU_IFMAP_FLOW_NUMBER; ++flow_idx) {
        while (memx_list_count(cascade->ifmap_queues[flow_idx]) > 0) {
            void *ifmap_data = memx_list_pop(cascade->ifmap_queues[flow_idx]);
            free(ifmap_data);
            ifmap_data = NULL;
        }
        memx_list_destroy(cascade->ifmap_queues[flow_idx]);
    }

    // clean-up output feature maps in queue
    for (int32_t flow_idx = 0; flow_idx < _CASCADE_MPU_OFMAP_FLOW_NUMBER; ++flow_idx) {
        while (memx_list_count(cascade->ofmap_queues[flow_idx]) > 0) {
            void *ofmap_data = memx_list_pop(cascade->ofmap_queues[flow_idx]);
            free(ofmap_data);
            ofmap_data = NULL;
        }
        memx_list_destroy(cascade->ofmap_queues[flow_idx]);
    }

    // clean-up jobs in queue
    while (memx_list_count(cascade->ifmap_job_queue.queue) > 0) {
        MemxMpuJob *job = (MemxMpuJob *)memx_list_pop(cascade->ifmap_job_queue.queue);
        free(job);
        job = NULL;
    }
    platform_mutex_destory(&cascade->ifmap_job_queue.guard);
    memx_list_destroy(cascade->ifmap_job_queue.queue);

    while (memx_list_count(cascade->ofmap_job_queue.queue) > 0) {
        MemxMpuJob *job = (MemxMpuJob *)memx_list_pop(cascade->ofmap_job_queue.queue);
        free(job);
        job = NULL;
    }
    platform_mutex_destory(&cascade->ofmap_job_queue.guard);
    memx_list_destroy(cascade->ofmap_job_queue.queue);

    // clean-up ifmap ring-buffer data
    for (int32_t flow_idx = 0; flow_idx < _CASCADE_MPU_IFMAP_FLOW_NUMBER; ++flow_idx) {
        free(cascade->ifmap_ringbuffers[flow_idx].Cascade.data);
        cascade->ifmap_ringbuffers[flow_idx].Cascade.data = NULL;
        platform_mutex_destory(&cascade->ifmap_ringbuffers[flow_idx].Cascade.guard);
        platform_mutex_destory(&cascade->ifmap_ringbuffers[flow_idx].Cascade.read_guard);
        platform_mutex_destory(&cascade->ifmap_ringbuffers[flow_idx].Cascade.write_guard);
    }
    platform_mutex_destory(&cascade->ifmap_ota_entry_count.guard);

    // clean-up ofmap ring-buffer data
    for (int32_t flow_idx = 0; flow_idx < _CASCADE_MPU_OFMAP_FLOW_NUMBER; ++flow_idx) {
        free(cascade->ofmap_ringbuffers[flow_idx].Cascade.data);
        cascade->ofmap_ringbuffers[flow_idx].Cascade.data = NULL;
        platform_mutex_destory(&cascade->ofmap_ringbuffers[flow_idx].Cascade.guard);
        platform_mutex_destory(&cascade->ofmap_ringbuffers[flow_idx].Cascade.read_guard);
        platform_mutex_destory(&cascade->ofmap_ringbuffers[flow_idx].Cascade.write_guard);
    }

    // clean-up ofmap hpoc setting
    for (int32_t flow_idx = 0; flow_idx < _CASCADE_MPU_OFMAP_FLOW_NUMBER; ++flow_idx) {
        if (cascade->ofmap_infos[flow_idx].Cascade.hpoc_dummy_channel_indexes != NULL) {
            free(cascade->ofmap_infos[flow_idx].Cascade.hpoc_dummy_channel_indexes);
            cascade->ofmap_infos[flow_idx].Cascade.hpoc_dummy_channel_indexes = NULL;
        }
    }

    // clean-up self
    free(cascade);
    cascade = NULL;
}

memx_status memx_cascade_reconfigure(MemxMpu *mpu, int32_t timeout)
{
    unused(mpu);
    unused(timeout);

    // do nothing for now
    // TODO: may need to stop and re-configure IO handle
    return MEMX_STATUS_OK;
}

memx_status memx_cascade_operation(MemxMpu *mpu, int32_t cmd_id, void *data, uint32_t size, int32_t timeout)
{
    if (!mpu) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    if (!mpu->mpuio) { return MEMX_STATUS_MPU_INVALID_IO_CONTEXT; }

    // use command ID to lookup operation, forward to MPUIO if command is unrecognized
    // forward all to mpuio for now
    return memx_mpuio_operation(mpu->mpuio, mpu->input_chip_id, cmd_id, data, size, timeout);
}

memx_status memx_cascade_stream_ifmap(MemxMpu *mpu, uint8_t flow_id, void *from_user_ifmap_data_buffer, int32_t delay_time_in_ms)
{
    if (!mpu || !mpu->context) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    MemxCascade *cascade = (MemxCascade *)mpu->context;

    if (flow_id >= _CASCADE_MPU_IFMAP_FLOW_NUMBER) { return MEMX_STATUS_MPU_INVALID_FLOW_ID; }

    MemxMpuIfmapInfo *ifmap_info = &cascade->ifmap_infos[flow_id];
    // encode data to be transferred only if background worker is enabled
    if (ifmap_info->valid == 0) { return MEMX_STATUS_MPU_IFMAP_NOT_CONFIG; }


    if (!from_user_ifmap_data_buffer) { return MEMX_STATUS_MPU_INVALID_DATA; }
    if (delay_time_in_ms < 0) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }


    // wait until queue has space, use polling here to detect timeout
    // do not use gettimeofdate() to acquire time for it's penalty just too high in embedded system
    int32_t milliSeconds_count = 0;
    while (memx_list_count(cascade->ifmap_queues[flow_id]) >= cascade->ifmap_queue_size) { // full
        if ((delay_time_in_ms != 0) &&
            (milliSeconds_count++ > delay_time_in_ms)) {
            return MEMX_STATUS_MPU_IFMAP_ENQUEUE_TIMEOUT;
        }
        platform_usleep(1000);
    }

    // allocate input feature map buffer
    // special usage for gbf80 to use size after padding
    void *to_chip_ifmap_data = malloc((size_t)ifmap_info->fmap.user_frame_size * ifmap_info->fmap.channel_size);
    if (!to_chip_ifmap_data) { return MEMX_STATUS_MPU_IFMAP_ALLOCATE_FAIL; }
    memset(to_chip_ifmap_data, 0x0, (size_t)ifmap_info->fmap.user_frame_size * ifmap_info->fmap.channel_size);

    // copy user data to queue, although data copy has large penalty
    // we still need to copy from user to prevent pointer being released
    memcpy(to_chip_ifmap_data, from_user_ifmap_data_buffer, (size_t)ifmap_info->fmap.user_frame_size * ifmap_info->fmap.channel_size);

    // put input feature map to queue
    if (memx_list_push(cascade->ifmap_queues[flow_id], to_chip_ifmap_data)) {
        free(to_chip_ifmap_data);
        to_chip_ifmap_data = NULL;
        printf("push ifmap data to ifmap_queues[flow_id(%d)] fail\n", flow_id);
        return MEMX_STATUS_MPU_IFMAP_ENQUEUE_FAIL;
    }

    // create new job to job queue
    if (memx_status_error(_memx_cascade_ifmap_job_enqueue(mpu, flow_id, (job_action_t)&_memx_cascade_job_ifmap_encode))) {
        memx_list_remove(cascade->ifmap_queues[flow_id], to_chip_ifmap_data);
        free(to_chip_ifmap_data);
        to_chip_ifmap_data = NULL;
        printf("push encode job to ifmap_job_queue[flow_id(%d)] fail\n", flow_id);
        return MEMX_STATUS_MPU_IFMAP_ENQUEUE_FAIL;
    }

    return MEMX_STATUS_OK;
}

memx_status memx_cascade_stream_ofmap(MemxMpu *mpu, uint8_t flow_id, void *to_user_ofmap_data_buffer, int32_t delay_time_in_ms)
{
    if (!mpu || !mpu->context) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    MemxCascade *cascade = (MemxCascade *)mpu->context;

    if (flow_id >= _CASCADE_MPU_OFMAP_FLOW_NUMBER) { return MEMX_STATUS_MPU_INVALID_FLOW_ID; }

    MemxMpuOfmapInfo *ofmap_info = &cascade->ofmap_infos[flow_id];
    // take data from internal buffer only if background worker is enabled
    if (ofmap_info->Cascade.valid == 0) { return MEMX_STATUS_MPU_OFMAP_NOT_CONFIG; }

    if (!to_user_ofmap_data_buffer) { return MEMX_STATUS_MPU_INVALID_DATA; }
    if (delay_time_in_ms < 0) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }

    long long milliSeconds_count = 0;
    // wait until there is something in queue, use polling here to detect timeout
    // do not use gettimeofdate() to acquire time for it's penalty just too high in embedded system
    while (memx_list_count(cascade->ofmap_queues[flow_id]) == 0) { // empty
        if ((delay_time_in_ms != 0) &&
            (milliSeconds_count++ > delay_time_in_ms)) {
            return MEMX_STATUS_MPU_OFMAP_DEQUEUE_TIMEOUT;
        }
        platform_usleep(1000);
    }

    // pop output feature map from queue
    void *from_chip_ofmap_data = memx_list_pop(cascade->ofmap_queues[flow_id]);
    if (!from_chip_ofmap_data) { return MEMX_STATUS_MPU_OFMAP_DEQUEUE_FAIL; }
    // copy from queue to user buffer
    // cannot pop internal buffer address to user directly or is there any way to give rvalue in C language?
    memcpy(to_user_ofmap_data_buffer, from_chip_ofmap_data, (uint64_t)(ofmap_info->Cascade.fmap.user_frame_size) * ofmap_info->Cascade.fmap.channel_size);

    // release resource allocated
    free(from_chip_ofmap_data);
    from_chip_ofmap_data = NULL;
    return MEMX_STATUS_OK;
}

memx_status memx_cascade_download_firmware(MemxMpu *mpu, const char *file_path)
{
    if (!mpu || !mpu->context || !file_path) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    if (!mpu->mpuio) { return MEMX_STATUS_MPU_INVALID_IO_CONTEXT; }

    // download model to device first then configure Host driver secondly
    return memx_mpuio_download_firmware(mpu->mpuio, file_path);
}

memx_status memx_cascade_download_model(MemxMpu* mpu, void * pDfpMeta, uint8_t model_idx, int32_t type, int32_t timeout)
{
    if (!mpu || !mpu->context) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    if (!mpu->mpuio) { return MEMX_STATUS_MPU_INVALID_IO_CONTEXT; }

    // download model to device first then configure Host driver secondly
    return memx_mpuio_download_model(mpu->mpuio, mpu->input_chip_id, pDfpMeta, model_idx, type, timeout);
}

memx_status memx_cascade_set_worker_number(MemxMpu* mpu, int32_t worker_number, int32_t timeout)
{
  memx_status status = MEMX_STATUS_OK;
  MemxCascade* cascade = NULL;

  if(memx_status_no_error(status)&&(mpu == NULL))
    status = MEMX_STATUS_MPU_INVALID_CONTEXT;
  if(memx_status_no_error(status)&&(mpu->context == NULL))
    status = MEMX_STATUS_MPU_INVALID_CONTEXT;
  if(memx_status_no_error(status)&&(worker_number <= 0))
    status = MEMX_STATUS_MPU_INVALID_WORKER_NUMBER;

  if(memx_status_no_error(status))
    cascade = (MemxCascade*)mpu->context;

  // kill job worker on number decreasing
  while(memx_status_no_error(status)&&(memx_list_count(cascade->ifmap_job_worker_infos) > worker_number)) {
      MemxMpuWorkerInfo* job_worker_info = (MemxMpuWorkerInfo *)memx_list_pop(cascade->ifmap_job_worker_infos);
      // go to sleep first to make sure not discard any on-going data
      _memx_mpu_worker_go_to_state(job_worker_info, _mpu_worker_state_pending);
      pthread_broadcast_signal_with_lock(&cascade->ifmap_job_queue.enqueue, &cascade->ifmap_job_queue.guard);
      _memx_mpu_worker_wait_state_entered(job_worker_info, _mpu_worker_state_pending);

      // disable worker
      job_worker_info->enable = 0;
      platform_cond_signal(&job_worker_info->state_changed);
      pthread_broadcast_signal_with_lock(&cascade->ifmap_job_queue.enqueue, &cascade->ifmap_job_queue.guard);
      platform_thread_join(&job_worker_info->worker, NULL);
  }
  while(memx_status_no_error(status)&&(memx_list_count(cascade->ofmap_job_worker_infos) > worker_number)) {
      MemxMpuWorkerInfo* job_worker_info = (MemxMpuWorkerInfo *)memx_list_pop(cascade->ofmap_job_worker_infos);
      // go to sleep first to make sure not discard any on-going data
      _memx_mpu_worker_go_to_state(job_worker_info, _mpu_worker_state_pending);
      pthread_broadcast_signal_with_lock(&cascade->ofmap_job_queue.enqueue, &cascade->ofmap_job_queue.guard);
      _memx_mpu_worker_wait_state_entered(job_worker_info, _mpu_worker_state_pending);

      // disable worker
      job_worker_info->enable = 0;
      _memx_mpu_worker_go_to_state(job_worker_info, _mpu_worker_state_pending);
      pthread_broadcast_signal_with_lock(&cascade->ofmap_job_queue.enqueue, &cascade->ofmap_job_queue.guard);
      platform_thread_join(&job_worker_info->worker, NULL);
  }
  // create job worker on number increasing
  while(memx_status_no_error(status)&&(memx_list_count(cascade->ifmap_job_worker_infos) < worker_number)) {
    MemxMpuWorkerInfo* job_worker_info = (MemxMpuWorkerInfo *)malloc(sizeof(MemxMpuWorkerInfo));
    if(job_worker_info != NULL) {
      job_worker_info->enable = 1;
      job_worker_info->mpu = mpu;
      job_worker_info->state = _mpu_worker_state_running;
      job_worker_info->state_next = _mpu_worker_state_running;
      if(platform_cond_init(&job_worker_info->state_changed, NULL)
        ||platform_mutex_create(&job_worker_info->guard, NULL)
        ||platform_thread_create(&job_worker_info->worker, NULL, &_memx_cascade_ifmap_job_worker_dowork, job_worker_info)
        ||(memx_list_push(cascade->ifmap_job_worker_infos, job_worker_info) != 0))
        status = MEMX_STATUS_MPU_WORKER_CREATE_FAIL;

    } else {
      status = MEMX_STATUS_MPU_WORKER_CREATE_FAIL;
    }
  }
  while(memx_status_no_error(status)&&(memx_list_count(cascade->ofmap_job_worker_infos) < worker_number)) {
    MemxMpuWorkerInfo* job_worker_info = (MemxMpuWorkerInfo *)malloc(sizeof(MemxMpuWorkerInfo));
    if(job_worker_info != NULL) {
      job_worker_info->enable = 1;
      job_worker_info->mpu = mpu;
      job_worker_info->state = _mpu_worker_state_running;
      job_worker_info->state_next = _mpu_worker_state_running;
      if (platform_cond_init(&job_worker_info->state_changed, NULL)
        ||platform_mutex_create(&job_worker_info->guard, NULL)
        ||platform_thread_create(&job_worker_info->worker, NULL, &_memx_cascade_ofmap_job_worker_dowork, job_worker_info)
        ||(memx_list_push(cascade->ofmap_job_worker_infos, job_worker_info) != 0))
        status = MEMX_STATUS_MPU_WORKER_CREATE_FAIL;
    } else {
      status = MEMX_STATUS_MPU_WORKER_CREATE_FAIL;
    }
  }

  if(memx_status_no_error(status))
    cascade->job_worker_number = worker_number;

  unused(timeout);
  return status;
}

memx_status memx_cascade_set_stream_enable(MemxMpu *mpu, int32_t wait, int32_t timeout)
{
    unused(timeout);

    if (!mpu || !mpu->context) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    if (!mpu->mpuio || !mpu->mpuio->context) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    MemxCascade *cascade = (MemxCascade *)mpu->context;

    // enable all workers
    _memx_mpu_worker_go_to_state(&cascade->ifmap_worker_info, _mpu_worker_state_running);
    _memx_mpu_worker_go_to_state(&cascade->ofmap_worker_info, _mpu_worker_state_running);

    // wait until all worker state to running
    if (wait) {
        _memx_mpu_worker_wait_state_entered(&cascade->ifmap_worker_info, _mpu_worker_state_running);
        _memx_mpu_worker_wait_state_entered(&cascade->ofmap_worker_info, _mpu_worker_state_running);
    }
    return MEMX_STATUS_OK;
}

memx_status memx_cascade_set_stream_disable(MemxMpu *mpu, int32_t wait, int32_t timeout)
{
    unused(timeout);

    if (!mpu || !mpu->context) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    MemxCascade *cascade = (MemxCascade *)mpu->context;

    // block all workers
    // signal to wake-up threads if they are waiting for specific condition
    _memx_mpu_worker_go_to_state(&cascade->ifmap_worker_info, _mpu_worker_state_pending);
    _memx_mpu_worker_go_to_state(&cascade->ofmap_worker_info, _mpu_worker_state_pending);
    pthread_cond_signal_with_lock(&cascade->ifmap_ota_entry_count.value_changed, &cascade->ifmap_ota_entry_count.guard);

    // wait until all worker state to pending
    // no need to wait for job workers for they only transfer data between queue and buffer
    if (wait) {
        _memx_mpu_worker_wait_state_entered(&cascade->ifmap_worker_info, _mpu_worker_state_pending);
        _memx_mpu_worker_wait_state_entered(&cascade->ofmap_worker_info, _mpu_worker_state_pending);
    }
    return MEMX_STATUS_OK;
}

memx_status memx_cascade_set_ifmap_queue_size(MemxMpu *mpu, int32_t size, int32_t timeout)
{
    unused(timeout);
    if (!mpu || !mpu->context) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    MemxCascade *cascade = (MemxCascade *)mpu->context; // get context here
    if (size <= 0) { return MEMX_STATUS_MPU_INVALID_QUEUE_SIZE; }

    cascade->ifmap_queue_size = size;
    return MEMX_STATUS_OK;
}

memx_status memx_cascade_set_ifmap_size(MemxMpu *mpu, uint8_t flow_id, int32_t height, int32_t width, int32_t z, int32_t channel_number, int32_t format, int32_t timeout)
{
    if (!mpu || !mpu->context) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    MemxCascade *cascade = (MemxCascade*)mpu->context; // get context here
    if (flow_id >= _CASCADE_MPU_IFMAP_FLOW_NUMBER) { return MEMX_STATUS_MPU_INVALID_FLOW_ID; }
    MemxMpuIfmapInfo *ifmap_info = &cascade->ifmap_infos[flow_id]; // pick the input feature map info structure based on given flow id
    MemxMpuFmapRingBuffer *ifmap_ringbuffer = &cascade->ifmap_ringbuffers[flow_id]; // internal ring-buffer stores ofmaps with alignement to be transferred
    if (height <= 0 || width <= 0 || z != 1 || channel_number <= 0) { return MEMX_STATUS_MPU_INVALID_FMAP_SIZE; } // cascade only support z=1
    if (format < MEMX_MPU_IFMAP_FORMAT_GBF80 || format >= MEMX_MPU_IFMAP_FORMAT_RESERVED) { return MEMX_STATUS_MPU_INVALID_FMAP_FORMAT; }

    // TODO: check there still data in queue and is not yet finished

    // compare to original stored information to see if configuration changed is required
    int32_t is_any_ifmap_info_changed = ((ifmap_info->fmap.height != height) ||
                                     (ifmap_info->fmap.width != width) ||
                                     (ifmap_info->fmap.channel_number != channel_number) ||
                                     (ifmap_info->fmap.format != format)) ? 1: 0;
    if (!is_any_ifmap_info_changed) { return MEMX_STATUS_OK; }

    // stop background worker first if configuration is to be changed
    ifmap_info->valid = 0;
    ifmap_ringbuffer->Cascade.valid = 0; // pause flow data
    memx_status status = _memx_cacasde_ifmap_info_update(mpu, flow_id, height, width, z, channel_number, format);
    if (memx_status_error(status)) { return  status; }

    // -----------------------------------
    // forward setting to low level driver
    // -----------------------------------
    status = memx_mpuio_set_ifmap_size(mpu->mpuio, mpu->input_chip_id, flow_id, ifmap_info->fmap.height,
            ifmap_info->fmap.width, ifmap_info->fmap.z, ifmap_info->fmap.fmap_channel_number, ifmap_info->fmap.format, timeout);
    if (memx_status_error(status)) { return  status; }

    // store new input feature map size configuration
    // enable flow background worker flow if size is none zero
    status = _memx_cascade_ifmap_ringbuffer_update(mpu, flow_id);
    if (memx_status_error(status)) { return  status; }

    ifmap_ringbuffer->Cascade.valid = 1; // resume flow data
    ifmap_info->valid = 1;

    return MEMX_STATUS_OK;
}

memx_status memx_cascade_set_ifmap_range_convert(MemxMpu *mpu, uint8_t flow_id, float shift, float scale, int32_t timeout)
{
    unused(timeout);

    if (!mpu || !mpu->context) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    MemxCascade *cascade = (MemxCascade*)mpu->context; // get context here
    if (flow_id >= _CASCADE_MPU_IFMAP_FLOW_NUMBER) { return MEMX_STATUS_MPU_INVALID_FLOW_ID; }
    MemxMpuIfmapInfo *ifmap_info = &cascade->ifmap_infos[flow_id]; // pick the input feature map info structure based on given flow id
    if (ifmap_info->valid == 0) { return MEMX_STATUS_MPU_IFMAP_NOT_CONFIG; }
    MemxMpuFmapRingBuffer *ifmap_ringbuffer = &cascade->ifmap_ringbuffers[flow_id]; // internal ring-buffer stores ofmaps with alignement to be transferred

    // stop background worker before input feature map shape changed
    ifmap_info->valid = 0;
    ifmap_ringbuffer->Cascade.valid = 0;
    memx_status status = _memx_cascade_ifmap_info_update_range_convert(mpu, flow_id, shift, scale);
    if (memx_status_error(status)) { return  status; }

    // -----------------------------------
    // forward setting to low level driver
    // -----------------------------------
    status = memx_mpuio_set_ifmap_size(mpu->mpuio, mpu->input_chip_id, flow_id, ifmap_info->fmap.height,
            ifmap_info->fmap.width, ifmap_info->fmap.z, ifmap_info->fmap.fmap_channel_number, ifmap_info->fmap.format, timeout);
    if (memx_status_error(status)) { return  status; }

    // start background worker with new setting
    status = _memx_cascade_ifmap_ringbuffer_update(mpu, flow_id);
    if (memx_status_error(status)) { return  status; }

    ifmap_ringbuffer->Cascade.valid = 1;
    ifmap_info->valid = 1;

    return MEMX_STATUS_OK;
}

memx_status memx_cascade_set_ofmap_queue_size(MemxMpu *mpu, int32_t size, int32_t timeout)
{
    unused(timeout);
    if (!mpu || !mpu->context) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    MemxCascade *cascade = (MemxCascade *)mpu->context; // get context here
    if (size <= 0) { return MEMX_STATUS_MPU_INVALID_QUEUE_SIZE; }

    cascade->ofmap_queue_size = size;
    return MEMX_STATUS_OK;
}

memx_status memx_cascade_set_ofmap_size(MemxMpu *mpu, uint8_t flow_id, int32_t height, int32_t width, int32_t z, int32_t channel_number, int32_t format, int32_t timeout)
{
    if (!mpu || !mpu->context) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    MemxCascade *cascade = (MemxCascade*)mpu->context; // get context here
    if (flow_id >= _CASCADE_MPU_OFMAP_FLOW_NUMBER) { return MEMX_STATUS_MPU_INVALID_FLOW_ID; }
    MemxMpuOfmapInfo *ofmap_info = &cascade->ofmap_infos[flow_id]; // pick the output feature map info structure based on given flow id
    MemxMpuFmapRingBuffer *ofmap_ringbuffer = &cascade->ofmap_ringbuffers[flow_id]; // internal ring-buffer stores ofmaps with alignement to be transferred
    if (height <= 0 || width <= 0 || z != 1 || channel_number <= 0) { return MEMX_STATUS_MPU_INVALID_FMAP_SIZE; } // cascade only support z=1
    if (format < MEMX_MPU_OFMAP_FORMAT_GBF80 || format >= MEMX_MPU_OFMAP_FORMAT_RESERVED) { return MEMX_STATUS_MPU_INVALID_FMAP_FORMAT; }


    // TODO: check there still data in queue and is not yet finished

    // compare to original stored information to see if configuration changed is required
    int32_t is_any_ofmap_info_changed = ((ofmap_info->Cascade.fmap.height != height) ||
                                     (ofmap_info->Cascade.fmap.width != width) ||
                                     (ofmap_info->Cascade.fmap.channel_number != channel_number) ||
                                     (ofmap_info->Cascade.fmap.format != format)) ? 1: 0;

    // backend mpuio and frontend mpu model_id mismatch or fmap info change, do update fmap size and backend mpuio model_id
    if ((mpu->model_id != mpu->mpuio->model_id) ||
        (is_any_ofmap_info_changed == 1)) {
        mpu->mpuio->model_id = mpu->model_id;
        // -----------------------------------
        // forward setting to low level driver
        // -----------------------------------
        memx_status status = memx_mpuio_set_ofmap_size(mpu->mpuio, mpu->output_chip_id, flow_id, height, width, z, channel_number, format, timeout);
        if (memx_status_error(status)) { return  status; }
    }

    // skip non-changed case
    if (!is_any_ofmap_info_changed) { return MEMX_STATUS_OK; }

    // stop background worker first if configuration is to be changed
    ofmap_info->Cascade.valid = 0;
    ofmap_ringbuffer->Cascade.valid = 0; // pause flow data
    memx_status status = _memx_cacasde_ofmap_info_update(mpu, flow_id, height, width, z, channel_number, format);
    if (memx_status_error(status)) { return  status; }

    // store new output feature map size configuration
    // enable flow background worker flow if size is none zero
    status = _memx_cascade_ofmap_ringbuffer_update(mpu, flow_id);
    if (memx_status_error(status)) { return  status; }

    ofmap_ringbuffer->Cascade.valid = 1; // resume flow data
    ofmap_info->Cascade.valid = 1;

    return MEMX_STATUS_OK;
}

memx_status memx_cascade_set_ofmap_hpoc(MemxMpu *mpu, uint8_t flow_id, int32_t hpoc_size, int32_t *hpoc_indexes, int32_t timeout)
{
    unused(timeout);
    if (!mpu || !mpu->context) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    MemxCascade *cascade = (MemxCascade *)mpu->context; // get context here
    if (flow_id >= _CASCADE_MPU_OFMAP_FLOW_NUMBER) { return MEMX_STATUS_MPU_INVALID_FLOW_ID; }
    MemxMpuOfmapInfo *ofmap_info = &cascade->ofmap_infos[flow_id]; // pick the output feature map info structure based on given flow id
    if (ofmap_info->Cascade.valid == 0) {return MEMX_STATUS_MPU_OFMAP_NOT_CONFIG; }
    MemxMpuFmapRingBuffer *ofmap_ringbuffer = &cascade->ofmap_ringbuffers[flow_id]; // internal ring-buffer stores ofmaps with alignement to be transferred
    if (hpoc_size <= 0 || !hpoc_indexes) { return MEMX_STATUS_MPU_INVALID_PARAMETER; }

    // stop background worker before output feature map shape changed
    ofmap_info->Cascade.valid = 0;
    ofmap_ringbuffer->Cascade.valid = 0;
    memx_status status = _memx_cascade_ofmap_info_update_hpoc(mpu, flow_id, hpoc_size, hpoc_indexes);
    if (memx_status_error(status)) { return  status; }

    status = memx_mpuio_set_ofmap_size(mpu->mpuio, mpu->output_chip_id, flow_id, ofmap_info->Cascade.fmap.height,
            ofmap_info->Cascade.fmap.width, ofmap_info->Cascade.fmap.z, ofmap_info->Cascade.fmap.fmap_channel_number, MEMX_MPU_OFMAP_FORMAT_GBF80, timeout);
    if (memx_status_error(status)) { return  status; }

    // start background worker with new setting
    status = _memx_cascade_ofmap_ringbuffer_update(mpu, flow_id);
    if (memx_status_error(status)) { return  status; }

    ofmap_ringbuffer->Cascade.valid = 1;
    ofmap_info->Cascade.valid = 1;

    return MEMX_STATUS_OK;
}

memx_status memx_cascade_update_fmap_size(MemxMpu* mpu, uint8_t in_flow_count, uint8_t out_flow_count, int32_t timeout)
{
    if (!mpu || !mpu->context) { return MEMX_STATUS_MPU_INVALID_CONTEXT; }
    if (in_flow_count == 0 || out_flow_count == 0) { return MEMX_STATUS_MPU_INVALID_FLOW_ID; }
    // -----------------------------------
    // forward setting to low level driver
    // -----------------------------------
    return memx_mpuio_update_fmap_size(mpu->mpuio, mpu->output_chip_id, in_flow_count, out_flow_count, timeout);
}
