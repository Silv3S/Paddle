// Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "paddle/fluid/framework/op_registry.h"

namespace paddle {
namespace operators {

template <typename DeviceContext, typename T>
class ClipNPUKernel : public framework::OpKernel<T> {
 public:
  void Compute(const framework::ExecutionContext& ctx) const override {
    auto* x = ctx.Input<phi::DenseTensor>("X");
    auto* out = ctx.Output<phi::DenseTensor>("Out");
    out->mutable_data<T>(ctx.GetPlace());

    auto min_tensor =
        ctx.HasInput("Min") ? ctx.Input<phi::DenseTensor>("Min") : nullptr;
    auto max_tensor =
        ctx.HasInput("Max") ? ctx.Input<phi::DenseTensor>("Max") : nullptr;

    phi::DenseTensor min_tensor_temp(x->type());
    phi::DenseTensor max_tensor_temp(x->type());
    if (min_tensor == nullptr) {
      auto min_value = static_cast<T>(ctx.Attr<float>("min"));
      min_tensor_temp.mutable_data<T>({1}, ctx.GetPlace());
      FillNpuTensorWithConstant<T>(&min_tensor_temp, min_value);
      min_tensor = &min_tensor_temp;
    }

    if (max_tensor == nullptr) {
      auto max_value = static_cast<T>(ctx.Attr<float>("max"));
      max_tensor_temp.mutable_data<T>({1}, ctx.GetPlace());
      FillNpuTensorWithConstant<T>(&max_tensor_temp, max_value);
      max_tensor = &max_tensor_temp;
    }

    auto stream =
        ctx.template device_context<paddle::platform::NPUDeviceContext>()
            .stream();
    const auto& runner =
        NpuOpRunner("ClipByValue", {*x, *min_tensor, *max_tensor}, {*out}, {});
    runner.Run(stream);
  }
};

template <typename DeviceContext, typename T>
class ClipGradNPUKernel : public framework::OpKernel<T> {
 public:
  void Compute(const framework::ExecutionContext& ctx) const override {
    auto* x = ctx.Input<phi::DenseTensor>("X");
    auto* dout = ctx.Input<phi::DenseTensor>(framework::GradVarName("Out"));
    auto* dx = ctx.Output<phi::DenseTensor>(framework::GradVarName("X"));
    dx->mutable_data<T>(ctx.GetPlace());

    auto* min_tensor =
        ctx.HasInput("Min") ? ctx.Input<phi::DenseTensor>("Min") : nullptr;
    auto* max_tensor =
        ctx.HasInput("Max") ? ctx.Input<phi::DenseTensor>("Max") : nullptr;

    auto min_val = ctx.Attr<float>("min");
    if (min_tensor) {
      phi::DenseTensor min_data;
      framework::TensorCopy(
          *min_tensor,
          platform::CPUPlace(),
          ctx.template device_context<platform::DeviceContext>(),
          &min_data);
      ctx.template device_context<paddle::platform::NPUDeviceContext>().Wait();
      min_val = static_cast<float>(min_data.data<T>()[0]);
    }

    auto max_val = ctx.Attr<float>("max");
    if (max_tensor) {
      phi::DenseTensor max_data;
      framework::TensorCopy(
          *max_tensor,
          platform::CPUPlace(),
          ctx.template device_context<platform::DeviceContext>(),
          &max_data);
      ctx.template device_context<paddle::platform::NPUDeviceContext>().Wait();
      max_val = static_cast<float>(max_data.data<T>()[0]);
    }

    auto stream =
        ctx.template device_context<paddle::platform::NPUDeviceContext>()
            .stream();
    const auto& runner =
        NpuOpRunner("HardtanhGrad",
                    {*x, *dout},
                    {*dx},
                    {{"min_val", min_val}, {"max_val", max_val}});
    runner.Run(stream);
  }
};

}  // namespace operators
}  // namespace paddle

namespace ops = paddle::operators;
namespace plat = paddle::platform;

REGISTER_OP_NPU_KERNEL(
    clip,
    ops::ClipNPUKernel<plat::NPUDeviceContext, float>,
    ops::ClipNPUKernel<plat::NPUDeviceContext, plat::float16>);

REGISTER_OP_NPU_KERNEL(
    clip_grad,
    ops::ClipGradNPUKernel<plat::NPUDeviceContext, float>,
    ops::ClipGradNPUKernel<plat::NPUDeviceContext, plat::float16>);
