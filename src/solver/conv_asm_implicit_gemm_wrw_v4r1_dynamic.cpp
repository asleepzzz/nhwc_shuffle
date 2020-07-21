/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/
#include "miopen/solver.hpp"
#include "miopen/handle.hpp"
#include <miopen/conv/wrw_invoke_params.hpp>
#include <miopen/conv/invokers/impl_gemm_dynamic.hpp>
#include <miopen/generic_search.hpp>
#include <miopen/gcn_asm_utils.hpp>
#include "implicitgemm_util.hpp"

namespace miopen {
namespace solver {

// two possible configs:
//{  16, 128,  16,   2,   4,   4,   4,   4,   4,   4,  16,   1,  16,   1,    4,  64},
//{  16, 128,  16,   2,   4,   4,   4,   4,   4,   4,  16,   1,  16,   1,   16,  16}

// compute gemmk groups
static inline int GetImplicitGemmWrwV4R1DynamicGemmkGroups(const ConvolutionContext& ctx,
                                                           const int& GemmKPerBlock)
{
    int gemmk        = ctx.batch_sz * ctx.in_height * ctx.in_width;
    int gemmk_groups = 1;
    int tmp_gemmk_groups;
    for(int i = 0; i < 6; i++)
    {
        tmp_gemmk_groups = 1 << i;
        if(0 == (gemmk % (tmp_gemmk_groups * GemmKPerBlock)))
            gemmk_groups = tmp_gemmk_groups;
        else
            break;
    }
    return gemmk_groups;
}

// find wrw dynamic kernel by a simple algo
// check wether this kernel can be applicable
static inline bool FindImplicitGemmWrwV4R1DynamicKernel(const ConvolutionContext& ctx,
                                                        std::string& kernel_name,
                                                        int& block_size,
                                                        int& grid_size)
{
    int n                  = ctx.batch_sz;
    int k                  = ctx.n_inputs;
    int c                  = ctx.n_outputs;
    int ho                 = ctx.in_height;
    int wo                 = ctx.in_width;
    int y                  = ctx.kernel_size_h;
    int x                  = ctx.kernel_size_w;
    int GemmN              = c * y * x;
    int GemmM              = k;
    int GemmK              = n * ho * wo;
    int GemmNRepeat        = 2;
    int GemmNPerThreadSubC = 4;
    int GemmN0YXPerBlock   = 16;
    int GemmMPerBlock      = 128;
    int GemmKPerBlock      = 16;
    int GemmKGroups        = 1;

    if(c % (GemmNRepeat * GemmNPerThreadSubC) != 0)
        return false;
    if(GemmN % (GemmNRepeat * GemmNPerThreadSubC * GemmN0YXPerBlock) != 0)
        return false;
    if(GemmM % GemmMPerBlock != 0)
        return false;

    GemmKGroups = GetImplicitGemmWrwV4R1DynamicGemmkGroups(ctx, GemmKPerBlock);
    if(GemmK % (GemmKGroups * GemmKPerBlock) != 0)
        return false;

    block_size = 256;
    grid_size  = (GemmM / GemmMPerBlock) *
                (GemmN / (GemmNRepeat * GemmNPerThreadSubC * GemmN0YXPerBlock)) * GemmKGroups;

    if((ho * wo) % 4 == 0)
        kernel_name = "igemm_v4r1_dynamic_wrw_128x128x16_8x8_4x4x4x4x4x4_16x1x16x1_4x64";
    else
        kernel_name = "igemm_v4r1_dynamic_wrw_128x128x16_8x8_4x4x4x4x4x4_16x1x16x1_16x16";
    return true;
}

size_t ConvAsmImplicitGemmV4R1DynamicWrw::GetWorkspaceSize(const ConvolutionContext& ctx) const
{
    int k             = ctx.n_inputs;
    int c             = ctx.n_outputs;
    int y             = ctx.kernel_size_h;
    int x             = ctx.kernel_size_w;
    int ele_size      = 0;
    int gemmk_groups  = 0;
    int extra_groups  = 0;
    int GemmKPerBlock = 16;
    if(ctx.IsFp32())
        ele_size = sizeof(float);
    else
        ele_size = 2;

    gemmk_groups = GetImplicitGemmWrwV4R1DynamicGemmkGroups(ctx, GemmKPerBlock);

    if(gemmk_groups == 1)
        extra_groups = 0;
    else
        extra_groups = gemmk_groups;
    return k * c * y * x * ele_size * extra_groups;
}

bool ConvAsmImplicitGemmV4R1DynamicWrw::IsApplicable(const ConvolutionContext& ctx) const
{
    const auto device_name = ctx.GetStream().GetDeviceName();
    if(!(StartsWith(device_name, "gfx900") || StartsWith(device_name, "gfx906")))
        return false;

    if(!ctx.direction.IsBackwardWrW())
        return false;

    if(!ctx.Is2d())
        return false;

    if(!ctx.IsFp32())
        return false;

    if(!ctx.rmv.IsV3())
        return false;

    if(ctx.group_counts != 1)
        return false;

    std::string kernel_name;
    int block_size;
    int grid_size;

    return FindImplicitGemmWrwV4R1DynamicKernel(ctx, kernel_name, block_size, grid_size);
}

ConvSolution ConvAsmImplicitGemmV4R1DynamicWrw::GetSolution(const ConvolutionContext& ctx) const
{
    ConvSolution result;

    KernelInfo kernel;
    std::ostringstream options;

    int block_size;
    int grid_size;
    std::string kernel_name;
    bool ret = FindImplicitGemmWrwV4R1DynamicKernel(ctx, kernel_name, block_size, grid_size);

    if(!ret)
        MIOPEN_THROW("this kernel should not run with igemm dynamic!");

    int GemmKPerBlock = 16;
    int gemmk_groups  = GetImplicitGemmWrwV4R1DynamicGemmkGroups(ctx, GemmKPerBlock);

    kernel.kernel_file = "igemm_v4r1_wrw_dynamic.s";
    kernel.kernel_name = kernel_name;
    kernel.g_wk.clear();
    /* Note here, for API like hipHccModuleLaunchKernel(), hipExtModuleLaunchKernel()
    * grid dims is in unit of work item.
    * But for api like hipModuleLaunchKernel(), grid dim is in unit of block.
    */
    kernel.g_wk.push_back(grid_size * block_size);
    kernel.g_wk.push_back(1);
    kernel.g_wk.push_back(1);
    kernel.l_wk.clear();
    kernel.l_wk.push_back(block_size);
    kernel.l_wk.push_back(1);
    kernel.l_wk.push_back(1);

    GenerateClangDefsym(options, "ROCM_METADATA_VERSION", ctx.rmv.UseV3() ? 5 : 4);

    kernel.comp_options = options.str();

    MIOPEN_LOG_I2(kernel.kernel_file + ":" + kernel.kernel_name);

    result.construction_params.push_back(kernel);

    if(gemmk_groups > 1)
    {
        KernelInfo kernel_reduction;
        int reduction_per_thread     = 8;
        kernel_reduction.kernel_file = "wrw_reduction_hip.cpp";
        kernel_reduction.kernel_name = "wrw_reduction_hip";
        kernel_reduction.g_wk.clear();
        int block_size_reduction = 256;
        int grid_size_redcution  = ctx.n_outputs * ctx.n_inputs * ctx.kernel_size_h *
                                  ctx.kernel_size_w / (reduction_per_thread * block_size_reduction);
        kernel_reduction.g_wk.push_back(grid_size_redcution * block_size_reduction);
        kernel_reduction.g_wk.push_back(1);
        kernel_reduction.g_wk.push_back(1);
        kernel_reduction.l_wk.clear();
        kernel_reduction.l_wk.push_back(block_size_reduction);
        kernel_reduction.l_wk.push_back(1);
        kernel_reduction.l_wk.push_back(1);

        kernel_reduction.comp_options =
            std::string("-Wno-old-style-cast ") + std::string("-Wno-cast-align");

        result.construction_params.push_back(kernel_reduction);
    }

    result.workspce_sz     = GetWorkspaceSize(ctx);
    result.invoker_factory = conv::MakeImplGemmDynamicDataInvokerFactory(ctx);

    return result;
}

} // namespace solver
} // namespace miopen