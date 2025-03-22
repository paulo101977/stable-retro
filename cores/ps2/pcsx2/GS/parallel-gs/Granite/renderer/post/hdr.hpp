/* Copyright (c) 2017-2024 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include "render_graph.hpp"

namespace Granite
{
struct HDRDynamicExposureInterface
{
	virtual ~HDRDynamicExposureInterface() = default;
	virtual float get_exposure() const = 0;
};

struct HDROptions
{
	bool dynamic_exposure = true;
};

struct FrameParameters;

void setup_hdr_postprocess(RenderGraph &graph, const FrameParameters &frame,
                           const std::string &input, const std::string &output,
                           const HDROptions &options,
                           const HDRDynamicExposureInterface *iface = nullptr);
void setup_hdr_postprocess_compute(RenderGraph &graph, const FrameParameters &frame,
                                   const std::string &input, const std::string &output,
                                   const HDROptions &options,
                                   const HDRDynamicExposureInterface *iface = nullptr);

struct HDR10PQEncodingConfig
{
	float hdr_pre_exposure;
	float ui_pre_exposure;
};
void setup_hdr10_pq_encoding(RenderGraph &graph, const std::string &output,
                             const std::string &hdr_input, const std::string &ui_input,
                             const HDR10PQEncodingConfig &config,
                             const VkHdrMetadataEXT &static_metadata);
}
