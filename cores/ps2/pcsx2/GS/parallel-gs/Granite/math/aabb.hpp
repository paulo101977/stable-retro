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

#include "math.hpp"
#include "muglm/muglm_impl.hpp"

namespace Granite
{
class AABB
{
public:
	AABB(vec3 minimum_, vec3 maximum_)
	{
		minimum.v4 = vec4(minimum_, 1.0f);
		maximum.v4 = vec4(maximum_, 1.0f);
	}

	AABB() = default;

	vec3 get_coord(float dx, float dy, float dz) const;
	AABB transform(const mat4 &m) const;

	void expand(const AABB &aabb);

	const vec3 &get_minimum() const
	{
		return minimum.v3;
	}

	const vec3 &get_maximum() const
	{
		return maximum.v3;
	}

	const vec4 &get_minimum4() const
	{
		return minimum.v4;
	}

	const vec4 &get_maximum4() const
	{
		return maximum.v4;
	}

	vec4 &get_minimum4()
	{
		return minimum.v4;
	}

	vec4 &get_maximum4()
	{
		return maximum.v4;
	}

	vec3 get_corner(unsigned i) const
	{
		float x = i & 1 ? maximum.v3.x : minimum.v3.x;
		float y = i & 2 ? maximum.v3.y : minimum.v3.y;
		float z = i & 4 ? maximum.v3.z : minimum.v3.z;
		return vec3(x, y, z);
	}

	vec3 get_center() const
	{
		return minimum.v3 + (maximum.v3 - minimum.v3) * vec3(0.5f);
	}

	float get_radius() const
	{
		return 0.5f * distance(minimum.v3, maximum.v3);
	}

private:
	union
	{
		vec3 v3;
		vec4 v4;
	} minimum, maximum;
};
}