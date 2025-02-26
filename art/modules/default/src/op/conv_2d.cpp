/*
 * Copyright 2022 SenseTime Group Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <string.h>

#include "art/log.h"
#include "art/module.h"
#include "art/op.h"
#include "art/op_settings.h"
#include "art/op_tp.h"

#include "../utils/im2col.hpp"
#include "../utils/sgemm.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    op_t o;
    uint32_t num_output;
    uint32_t pad_h;
    uint32_t pad_w;
    uint32_t kernel_h;
    uint32_t kernel_w;
    uint32_t stride_h;
    uint32_t stride_w;
    uint32_t hole_h;
    uint32_t hole_w;
    uint32_t group;
    mem_t *temp_data; // temp mem for col in im2col
    mem_t *aux_mem_1; // temp mem for matrix A in sgemm
    mem_t *aux_mem_2; // temp mem for matrix C in sgemm
} op_conv_2d_t;
op_conv_2d_t *op_default_conv_2d_tp_alloc(workspace_t *ws);
void op_default_conv_2d_tp_config(op_t *op);
void op_default_conv_2d_tp_destroy(op_t *op);
void op_default_conv_2d_tp_dealloc(op_t *op);
void op_default_conv_2d_tp_prepare(op_t *op);

#ifdef __cplusplus
}
#endif

op_conv_2d_t *op_default_conv_2d_tp_alloc(workspace_t *ws)
{
    (void)ws;
    op_conv_2d_t *res = (op_conv_2d_t *)malloc(sizeof(op_conv_2d_t));
    memset(res, 0, sizeof(op_conv_2d_t));
    return res;
}

void op_default_conv_2d_tp_config(op_t *op)
{
    CHECK(op_setting_single_get(
        op, SETTING_CONV_2D_NUM_OUTPUT, dtUINT32, &((op_conv_2d_t *)op)->num_output));
    CHECK(op_setting_single_get(
        op, SETTING_CONV_2D_KERNEL_H, dtUINT32, &((op_conv_2d_t *)op)->kernel_h));
    CHECK(op_setting_single_get(
        op, SETTING_CONV_2D_KERNEL_W, dtUINT32, &((op_conv_2d_t *)op)->kernel_w));
    CHECK(op_setting_single_get(op, SETTING_CONV_2D_PAD_H, dtUINT32, &((op_conv_2d_t *)op)->pad_h));
    CHECK(op_setting_single_get(op, SETTING_CONV_2D_PAD_W, dtUINT32, &((op_conv_2d_t *)op)->pad_w));
    CHECK(op_setting_single_get(
        op, SETTING_CONV_2D_STRIDE_H, dtUINT32, &((op_conv_2d_t *)op)->stride_h));
    CHECK(op_setting_single_get(
        op, SETTING_CONV_2D_STRIDE_W, dtUINT32, &((op_conv_2d_t *)op)->stride_w));
    CHECK(
        op_setting_single_get(op, SETTING_CONV_2D_HOLE_H, dtUINT32, &((op_conv_2d_t *)op)->hole_h));
    CHECK(
        op_setting_single_get(op, SETTING_CONV_2D_HOLE_W, dtUINT32, &((op_conv_2d_t *)op)->hole_w));
    CHECK(op_setting_single_get(op, SETTING_CONV_2D_GROUP, dtUINT32, &((op_conv_2d_t *)op)->group));
}

void op_default_conv_2d_tp_destroy(op_t *op)
{
    if (((op_conv_2d_t *)op)->aux_mem_1)
        mem_delete(((op_conv_2d_t *)op)->aux_mem_1);
    if (((op_conv_2d_t *)op)->aux_mem_2)
        mem_delete(((op_conv_2d_t *)op)->aux_mem_2);
    if (((op_conv_2d_t *)op)->temp_data)
        mem_delete(((op_conv_2d_t *)op)->temp_data);
}

void op_default_conv_2d_tp_dealloc(op_t *op)
{
    if (NULL != op)
        free(op);
}

static void op_default_conv_2d_blas_run(op_t *op)
{
    size_t i;
    const op_conv_2d_t *conv_op = (op_conv_2d_t *)op;
    size_t kernel_size = conv_op->kernel_h * conv_op->kernel_w * op->input_tensors[0]->shape.dim[1];
    size_t batch_size = op->input_tensors[0]->shape.dim[0];
    size_t i_chw = op->input_tensors[0]->shape.dim[1] * op->input_tensors[0]->shape.dim[2]
        * op->input_tensors[0]->shape.dim[3];
    size_t o_hw = op->output_tensors[0].shape.dim[2] * op->output_tensors[0].shape.dim[3];
    size_t o_chw = o_hw * conv_op->num_output;
    const float *input_0 = (const float *)mem_cpu_data(op->input_tensors[0]->mem);
    const float *input_1 = (const float *)mem_cpu_data(op->input_tensors[1]->mem);
    float *output_0 = (float *)mem_cpu_data(op->output_tensors[0].mem);
    memset(output_0, 0, sizeof(float) * shape_count(&op->output_tensors[0].shape));

    for (i = 0; i < batch_size; ++i) {
        im2col(
            input_0, (float *)mem_cpu_data(conv_op->temp_data), op->input_tensors[0]->shape.dim[1],
            op->input_tensors[0]->shape.dim[2], op->input_tensors[0]->shape.dim[3],
            conv_op->kernel_h, conv_op->kernel_w, conv_op->pad_h, conv_op->pad_w, conv_op->stride_h,
            conv_op->stride_w, conv_op->hole_h, conv_op->hole_w);
        // cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans, conv_op->num_output, o_hw,
        // kernel_size,
        //     1.0, input_1, kernel_size, conv_op->temp_data, kernel_size, 0.0, output_0, o_hw);
        sgemm(
            conv_op->num_output, o_hw, kernel_size, input_1,
            (float *)mem_cpu_data(conv_op->temp_data), output_0, conv_op->aux_mem_1,
            conv_op->aux_mem_2);
        input_0 += i_chw;
        output_0 += o_chw;
    }
}

static void op_default_conv_2d_bias_blas_run(op_t *op)
{
    size_t i, j, k;
    const op_conv_2d_t *conv_op = (op_conv_2d_t *)op;
    size_t kernel_size = conv_op->kernel_h * conv_op->kernel_w * op->input_tensors[0]->shape.dim[1];
    // size_t batch_size = op->input_tensors[0]->shape.dim[0];
    size_t i_chw = op->input_tensors[0]->shape.dim[1] * op->input_tensors[0]->shape.dim[2]
        * op->input_tensors[0]->shape.dim[3];
    size_t o_hw = op->output_tensors[0].shape.dim[2] * op->output_tensors[0].shape.dim[3];
    size_t o_chw = o_hw * conv_op->num_output;
    const float *input_0 = (const float *)mem_cpu_data(op->input_tensors[0]->mem);
    const float *input_1 = (const float *)mem_cpu_data(op->input_tensors[1]->mem);
    const float *input_2 = (const float *)mem_cpu_data(op->input_tensors[2]->mem);
    float *output_0 = (float *)mem_cpu_data(op->output_tensors[0].mem);
    // float* output_0_temp = output_0;

    // prepare bias data
    for (i = 0; i < op->input_tensors[0]->shape.dim[0]; ++i) {
        for (j = 0; j < conv_op->num_output; ++j) {
            for (k = 0; k < o_hw; ++k) {
                *output_0++ = input_2[j];
            }
        }
    }
    output_0 -= op->input_tensors[0]->shape.dim[0] * o_chw;
    // for (i = 0; i < op->input_tensors[0]->shape.dim[0] * o_chw; ++i) {
    //     printf("%f\n", output_0[i]);
    // }

    for (i = 0; i < op->input_tensors[0]->shape.dim[0]; ++i) {
        im2col(
            input_0, (float *)mem_cpu_data(conv_op->temp_data), op->input_tensors[0]->shape.dim[1],
            op->input_tensors[0]->shape.dim[2], op->input_tensors[0]->shape.dim[3],
            conv_op->kernel_h, conv_op->kernel_w, conv_op->pad_h, conv_op->pad_w, conv_op->stride_h,
            conv_op->stride_w, conv_op->hole_h, conv_op->hole_w);
        // cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans, conv_op->num_output, o_hw,
        //     kernel_size, 1.0, input_1, kernel_size, conv_op->temp_data, kernel_size, 0.0,
        //     output_0, o_hw);
        sgemm(
            conv_op->num_output, o_hw, kernel_size, input_1,
            (float *)mem_cpu_data(conv_op->temp_data), output_0, conv_op->aux_mem_1,
            conv_op->aux_mem_2);
        input_0 += i_chw;
        output_0 += o_chw;
    }
}

static void op_default_conv_2d_group_blas_run(op_t *op)
{
    size_t i, j;
    const op_conv_2d_t *conv_op = (op_conv_2d_t *)op;
    size_t batch_size = op->input_tensors[0]->shape.dim[0];
    size_t group = conv_op->group;
    size_t g_channel = op->input_tensors[0]->shape.dim[1] / group;
    size_t g_num_output = conv_op->num_output / group;
    size_t kernel_size = conv_op->kernel_h * conv_op->kernel_w * g_channel;
    size_t g_kernel_count = kernel_size * g_num_output;
    size_t i_g_chw
        = g_channel * op->input_tensors[0]->shape.dim[2] * op->input_tensors[0]->shape.dim[3];
    size_t o_hw = op->output_tensors[0].shape.dim[2] * op->output_tensors[0].shape.dim[3];
    size_t o_g_chw = o_hw * g_num_output;
    const float *input_0 = (const float *)mem_cpu_data(op->input_tensors[0]->mem);
    const float *input_1 = (const float *)mem_cpu_data(op->input_tensors[1]->mem);
    float *output_0 = (float *)mem_cpu_data(op->output_tensors[0].mem);
    memset(output_0, 0, sizeof(float) * shape_count(&op->output_tensors[0].shape));

    for (i = 0; i < batch_size; ++i) {
        input_1 = (float *)mem_cpu_data(op->input_tensors[1]->mem);
        for (j = 0; j < group; ++j) {
            im2col(
                input_0, (float *)mem_cpu_data(conv_op->temp_data), g_channel,
                op->input_tensors[0]->shape.dim[2], op->input_tensors[0]->shape.dim[3],
                conv_op->kernel_h, conv_op->kernel_w, conv_op->pad_h, conv_op->pad_w,
                conv_op->stride_h, conv_op->stride_w, conv_op->hole_h, conv_op->hole_w);
            // cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans, g_num_output,o_hw,
            //     kernel_size, 1.0, input_1, kernel_size, conv_op->temp_data, kernel_size, 0.0,
            sgemm(
                g_num_output, o_hw, kernel_size, input_1, (float *)mem_cpu_data(conv_op->temp_data),
                output_0, conv_op->aux_mem_1, conv_op->aux_mem_2);
            input_0 += i_g_chw;
            input_1 += g_kernel_count;
            output_0 += o_g_chw;
        }
    }
}

static void op_default_conv_2d_group_bias_blas_run(op_t *op)
{
    size_t i, j, k;
    const op_conv_2d_t *conv_op = (op_conv_2d_t *)op;
    // size_t batch_size = op->input_tensors[0]->shape.dim[0];
    size_t group = conv_op->group;
    size_t g_channel = op->input_tensors[0]->shape.dim[1] / group;
    size_t g_num_output = conv_op->num_output / group;
    size_t kernel_size = conv_op->kernel_h * conv_op->kernel_w * g_channel;
    size_t g_kernel_count = kernel_size * g_num_output;
    size_t i_g_chw
        = g_channel * op->input_tensors[0]->shape.dim[2] * op->input_tensors[0]->shape.dim[3];
    size_t o_hw = op->output_tensors[0].shape.dim[2] * op->output_tensors[0].shape.dim[3];
    size_t o_g_chw = o_hw * g_num_output;
    const float *input_0 = (const float *)mem_cpu_data(op->input_tensors[0]->mem);
    const float *input_1 = (const float *)mem_cpu_data(op->input_tensors[1]->mem);
    const float *input_2 = (const float *)mem_cpu_data(op->input_tensors[2]->mem);
    float *output_0 = (float *)mem_cpu_data(op->output_tensors[0].mem);
    // float* output_0_temp = output_0;

    // prepare bias data
    for (i = 0; i < op->input_tensors[0]->shape.dim[0]; ++i) {
        for (j = 0; j < conv_op->num_output; ++j) {
            for (k = 0; k < o_hw; ++k) {
                *output_0++ = input_2[j];
            }
        }
    }
    output_0 -= op->input_tensors[0]->shape.dim[0] * conv_op->num_output * o_hw;

    for (i = 0; i < op->input_tensors[0]->shape.dim[0]; ++i) {
        input_1 = (float *)mem_cpu_data(op->input_tensors[1]->mem);
        for (j = 0; j < group; ++j) {
            im2col(
                input_0, (float *)mem_cpu_data(conv_op->temp_data), g_channel,
                op->input_tensors[0]->shape.dim[2], op->input_tensors[0]->shape.dim[3],
                conv_op->kernel_h, conv_op->kernel_w, conv_op->pad_h, conv_op->pad_w,
                conv_op->stride_h, conv_op->stride_w, conv_op->hole_h, conv_op->hole_w);
            // cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans, g_num_output, o_hw,
            //     kernel_size, 1.0, input_1, kernel_size, conv_op->temp_data, kernel_size, 1.0,
            //     output_0, o_hw);
            sgemm(
                g_num_output, o_hw, kernel_size, input_1, (float *)mem_cpu_data(conv_op->temp_data),
                output_0, conv_op->aux_mem_1, conv_op->aux_mem_2);
            input_0 += i_g_chw;
            input_1 += g_kernel_count;
            output_0 += o_g_chw;
        }
    }
}

void op_default_conv_2d_tp_prepare(op_t *op)
{
    int i;
    op_conv_2d_t *conv_op = (op_conv_2d_t *)op;
    size_t count;
    uint32_t group = conv_op->group;

    for (i = 0; i < op->input_size; ++i) {
        tensor_alloc(op->input_tensors[i]);
    }
    for (i = 0; i < op->output_size; ++i) {
        tensor_alloc(&op->output_tensors[i]);
    }
    if (NULL == conv_op->aux_mem_1)
        conv_op->aux_mem_1 = mem_new(cpu_mem_tp);
    // mem_alloc(conv_op->aux_mem_1, sizeof(float) * shape_count(&op->input_tensors[1]->shape));
    mem_alloc(
        conv_op->aux_mem_1, sizeof(float) * shape_count(&op->input_tensors[1]->shape) / group);
    if (NULL == conv_op->aux_mem_2)
        conv_op->aux_mem_2 = mem_new(cpu_mem_tp);
    // mem_alloc(conv_op->aux_mem_2, sizeof(float) * shape_count(&op->output_tensors[0].shape));
    mem_alloc(
        conv_op->aux_mem_2,
        sizeof(float) * op->output_tensors[0].shape.dim[1] * op->output_tensors[0].shape.dim[2]
            * op->output_tensors[0].shape.dim[3] / group);

    count = conv_op->kernel_h * conv_op->kernel_w * (op->input_tensors[0]->shape.dim[1] / group)
        * op->output_tensors[0].shape.dim[2] * op->output_tensors[0].shape.dim[3];

    switch (op->input_tensors[0]->dtype) {
    case dtFLOAT32:
        if (NULL == conv_op->temp_data)
            conv_op->temp_data = mem_new(cpu_mem_tp);
        mem_alloc(conv_op->temp_data, sizeof(float) * count);
        memset(mem_cpu_data(conv_op->temp_data), 0, sizeof(float) * count);
        if (1 == group) {
            if (2 == op->input_size) {
                op->run_func = op_default_conv_2d_blas_run;
            } else {
                op->run_func = op_default_conv_2d_bias_blas_run;
            }
        } else {
            if (2 == op->input_size) {
                op->run_func = op_default_conv_2d_group_blas_run;
            } else {
                op->run_func = op_default_conv_2d_group_bias_blas_run;
            }
        }
        break;
    default:
        CHECK(false);
        break;
    }
}
