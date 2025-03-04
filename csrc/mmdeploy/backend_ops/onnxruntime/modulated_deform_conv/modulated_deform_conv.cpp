// Copyright (c) OpenMMLab. All rights reserved
#include "modulated_deform_conv.h"

#include <cmath>
#include <vector>

#include "modulated_deform_conv/modulated_deform_conv_cpu.h"
#include "ort_utils.h"

namespace mmdeploy {

void gemm_ref_fp32(const float *A, const float *B, const float *V, const float *H,
                   const int32_t trans_A, const int32_t trans_B, const int32_t M, const int32_t N,
                   const int32_t K, const float alpha, const float beta, float *Y) {
  if (!trans_A && !trans_B) {  // MK, KN; NN
    for (int64_t m = 0; m < M; ++m) {
      for (int64_t n = 0; n < N; ++n) {
        float y = 0.0f;
        for (int64_t k = 0; k < K; ++k) {
          y += A[m * K + k] * B[k * N + n];
        }
        y *= alpha;
        if (V) y += beta * V[n];
        if (H) y += beta * H[m * N + n];
        Y[m * N + n] = y;
      }
    }
  }
  if (trans_A && !trans_B) {  // KM, KN; TN
    for (int64_t m = 0; m < M; ++m) {
      for (int64_t n = 0; n < N; ++n) {
        float y = 0.0f;
        for (int64_t k = 0; k < K; ++k) {
          y += A[k * M + m] * B[k * N + n];
        }
        y *= alpha;
        if (V) y += beta * V[n];
        if (H) y += beta * H[m * N + n];
        Y[m * N + n] = y;
      }
    }
  }
  if (trans_A && trans_B) {  // KM, NK; TT
    for (int64_t m = 0; m < M; ++m) {
      for (int64_t n = 0; n < N; ++n) {
        float y = 0.0f;
        for (int64_t k = 0; k < K; ++k) {
          y += A[k * M + m] * B[n * K + k];
        }
        y *= alpha;
        if (V) y += beta * V[n];
        if (H) y += beta * H[m * N + n];
        Y[m * N + n] = y;
      }
    }
  }
  if (!trans_A && trans_B) {  // MK, NK; NT
    for (int64_t m = 0; m < M; ++m) {
      for (int64_t n = 0; n < N; ++n) {
        float y = 0.0f;
        for (int64_t k = 0; k < K; ++k) {
          y += A[m * K + k] * B[n * K + k];
        }
        y *= alpha;
        if (V) y += beta * V[n];
        if (H) y += beta * H[m * N + n];
        Y[m * N + n] = y;
      }
    }
  }
}

void deformable_conv2d_ref_fp32(const float *src, const float *offset, const float *mask,
                                const float *filter, const float *bias, const int64_t batch,
                                const int64_t src_c, const int64_t src_h, const int64_t src_w,
                                const int64_t dst_c, const int64_t dst_h, const int64_t dst_w,
                                const int64_t group, const int64_t offset_group,
                                const int64_t channels, const int64_t num_output,
                                const int64_t kernel_h, const int64_t kernel_w,
                                const int64_t stride_h, const int64_t stride_w, const int64_t pad_h,
                                const int64_t pad_w, const int64_t dilation_h,
                                const int64_t dilation_w, float *columns, float *dst) {
  const int64_t ic_per_gp = channels / group;
  const int64_t oc_per_gp = num_output / group;

  for (int64_t b = 0; b < batch; ++b) {
    for (int64_t g = 0; g < group; ++g) {
      deformable_im2col_2d<float>(
          src + b * src_c * src_h * src_w + g * ic_per_gp * src_h * src_w,
          offset + b * offset_group * 2 * kernel_h * kernel_w * dst_h * dst_w,
          mask + b * offset_group * kernel_h * kernel_w * dst_h * dst_w, src_h, src_w, kernel_h,
          kernel_w, pad_h, pad_w, stride_h, stride_w, dilation_h, dilation_w, ic_per_gp,
          offset_group, dst_h, dst_w, mask != nullptr, columns);
      float *dst_ptr = dst + b * dst_c * dst_h * dst_w + g * oc_per_gp * dst_h * dst_w;
      if (bias != nullptr) {
        const float *bias_ptr = bias + g * oc_per_gp;
        for (int64_t oc = 0; oc < oc_per_gp; ++oc) {
          for (int64_t hw = 0; hw < dst_h * dst_w; ++hw) {
            dst_ptr[oc * dst_h * dst_w + hw] = bias_ptr[oc];
          }
        }
      } else {
        memset(dst_ptr, 0.0f, sizeof(float) * oc_per_gp * dst_h * dst_w);
      }
      gemm_ref_fp32(filter + g * oc_per_gp * ic_per_gp * kernel_h * kernel_w, columns, nullptr,
                    dst_ptr, 0, 0, oc_per_gp, dst_h * dst_w, ic_per_gp * kernel_h * kernel_w, 1.0f,
                    1.0f, dst_ptr);
    }
  }
}

MMCVModulatedDeformConvKernel::MMCVModulatedDeformConvKernel(const OrtApi &api,
                                                             const OrtKernelInfo *info)
    : ort_(api), info_(info) {
//  std::vector<int64_t> stride = ort_.KernelInfoGetAttribute<std::vector<int64_t>>(info, "stride");
#if ORT_API_VERSION >= 14
  const auto kernel_info = Ort::ConstKernelInfo(info);
  std::vector<int64_t> stride = kernel_info.GetAttributes<int64_t>("stride");
  std::vector<int64_t> padding = kernel_info.GetAttributes<int64_t>("padding");
  std::vector<int64_t> dilation = kernel_info.GetAttributes<int64_t>("dilation");

  deformable_group_ = kernel_info.GetAttribute<int64_t>("deform_groups");
  group_ = kernel_info.GetAttribute<int64_t>("groups");
#else
  Ort::CustomOpApi custom_api{api};
  auto stride = custom_api.KernelInfoGetAttribute<std::vector<int64_t> >(info, "stride");
  auto padding = custom_api.KernelInfoGetAttribute<std::vector<int64_t> >(info, "padding");
  auto dilation = custom_api.KernelInfoGetAttribute<std::vector<int64_t> >(info, "dilation");

  deformable_group_ = custom_api.KernelInfoGetAttribute<int64_t>(info, "deform_groups");
  group_ = custom_api.KernelInfoGetAttribute<int64_t>(info, "groups");
#endif

  stride_height_ = stride[0];
  stride_width_ = stride[1];
//  std::vector<int64_t> padding = ort_.KernelInfoGetAttribute<std::vector<int64_t>>(info, "padding");

  padding_height_ = padding[0];
  padding_width_ = padding[1];
//  std::vector<int64_t> dilation =
//      ort_.KernelInfoGetAttribute<std::vector<int64_t>>(info, "dilation");

  dilation_height_ = dilation[0];
  dilation_width_ = dilation[1];
//  deformable_group_ = ort_.KernelInfoGetAttribute<int64_t>(info, "deform_groups");
//  group_ = ort_.KernelInfoGetAttribute<int64_t>(info, "groups");

  // create allocator
  allocator_ = Ort::AllocatorWithDefaultOptions();
}

void MMCVModulatedDeformConvKernel::Compute(OrtKernelContext *context) {
  const int64_t stride_height = stride_height_;
  const int64_t stride_width = stride_width_;
  const int64_t padding_height = padding_height_;
  const int64_t padding_width = padding_width_;
  const int64_t dilation_height = dilation_height_;
  const int64_t dilation_width = dilation_width_;
  const int64_t deformable_group = deformable_group_;
  const int64_t group = group_;

//  const OrtValue *input = ort_.KernelContext_GetInput(context, 0);
//  const float *input_data = reinterpret_cast<const float *>(ort_.GetTensorData<float>(input));
//
//  const OrtValue *offset = ort_.KernelContext_GetInput(context, 1);
//  const float *offset_data = reinterpret_cast<const float *>(ort_.GetTensorData<float>(offset));
//
//  const OrtValue *mask = ort_.KernelContext_GetInput(context, 2);
//  const float *mask_data = reinterpret_cast<const float *>(ort_.GetTensorData<float>(mask));
//
//  const OrtValue *filter = ort_.KernelContext_GetInput(context, 3);
//  const float *filter_data = reinterpret_cast<const float *>(ort_.GetTensorData<float>(filter));
#if ORT_API_VERSION >= 14
  const Ort::KernelContext ctx(context);
  const auto input = ctx.GetInput(0);
  const auto offset = ctx.GetInput(1);
  const auto mask = ctx.GetInput(2);
  const auto filter = ctx.GetInput(3);
  const auto bias = ctx.GetInput(4);

  const float *bias_data = bias ? bias.GetTensorData<float>() : nullptr;
#else
  Ort::CustomOpApi api{ort_};
  const Ort::Unowned<Ort::Value> input =
      const_cast<OrtValue *>(api.KernelContext_GetInput(context, 0));
  const Ort::Unowned<Ort::Value> offset =
      const_cast<OrtValue *>(api.KernelContext_GetInput(context, 1));
  const Ort::Unowned<Ort::Value> mask =
      const_cast<OrtValue *>(api.KernelContext_GetInput(context, 2));
  const Ort::Unowned<Ort::Value> filter =
      const_cast<OrtValue *>(api.KernelContext_GetInput(context, 3));
  const float *bias_data = [&context, &api]() -> const float * {
    const OrtValue *bias_val = api.KernelContext_GetInput(context, 4);
    if (bias_val) {
      const Ort::Unowned<Ort::Value> bias{const_cast<OrtValue *>(bias_val)};
      return bias.GetTensorData<float>();
    }
    return nullptr;
  }();
#endif
//  const OrtValue *bias = ort_.KernelContext_GetInput(context, 4);
//  const float *bias_data = (bias != nullptr)
//                               ? reinterpret_cast<const float *>(ort_.GetTensorData<float>(bias))
//                               : nullptr;
//  // const float *bias_data = nullptr;
  const float *input_data = input.GetTensorData<float>();
  const float *offset_data = offset.GetTensorData<float>();
  const float *mask_data = mask.GetTensorData<float>();
  const float *filter_data = filter.GetTensorData<float>();

//  OrtTensorDimensions input_dims(ort_, input);
//  OrtTensorDimensions filter_dims(ort_, filter);

  std::vector<int64_t> input_dims = input.GetTensorTypeAndShapeInfo().GetShape();
  std::vector<int64_t> filter_dims = filter.GetTensorTypeAndShapeInfo().GetShape();

  int64_t batch = input_dims[0];
  int64_t channels = input_dims[1];
  int64_t in_height = input_dims[2];
  int64_t in_width = input_dims[3];
  int64_t num_output = filter_dims[0];
  int64_t kernel_height = filter_dims[2];
  int64_t kernel_width = filter_dims[3];

  // get output memory
  int64_t out_height = floor(
      (in_height + 2 * padding_height - dilation_height * (kernel_height - 1) - 1) / stride_height +
      1);
  int64_t out_width = floor(
      (in_width + 2 * padding_width - dilation_width * (kernel_width - 1) - 1) / stride_width + 1);

  std::vector<int64_t> output_dims = {batch, num_output, out_height, out_width};
//  OrtValue *output =
//      ort_.KernelContext_GetOutput(context, 0, output_dims.data(), output_dims.size());
//  float *out_ptr = ort_.GetTensorMutableData<float>(output);

#if ORT_API_VERSION >= 14
  auto output = ctx.GetOutput(0, output_dims.data(), output_dims.size());
#else
  Ort::Unowned<Ort::Value> output =
      api.KernelContext_GetOutput(context, 0, output_dims.data(), output_dims.size());
#endif

  float *out_ptr = output.GetTensorMutableData<float>();

  // allocate tmp memory
  int64_t column_len = (channels / group) * kernel_height * kernel_width * out_height * out_width;
  float *columns = (float *)allocator_.Alloc(sizeof(float) * column_len);

  deformable_conv2d_ref_fp32(input_data, offset_data, mask_data, filter_data, bias_data, batch,
                             channels, in_height, in_width, num_output, out_height, out_width,
                             group, deformable_group, channels, num_output, kernel_height,
                             kernel_width, stride_height, stride_width, padding_height,
                             padding_width, dilation_height, dilation_width, columns, out_ptr);
}
REGISTER_ONNXRUNTIME_OPS(mmdeploy, MMCVModulatedDeformConvOp);
REGISTER_ONNXRUNTIME_OPS(mmcv, MMCVModulatedDeformConvOp);
}  // namespace mmdeploy
