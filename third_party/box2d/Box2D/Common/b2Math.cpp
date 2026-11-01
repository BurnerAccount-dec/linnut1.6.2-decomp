/*
* Copyright (c) 2007-2009 Erin Catto http://www.gphysics.com
*
* This software is provided 'as-is', without any express or implied
* warranty.  In no event will the authors be held liable for any damages
* arising from the use of this software.
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
* 1. The origin of this software must not be misrepresented; you must not
* claim that you wrote the original software. If you use this software
* in a product, an acknowledgment in the product documentation would be
* appreciated but is not required.
* 2. Altered source versions must be plainly marked as such, and must not be
* misrepresented as being the original software.
* 3. This notice may not be removed or altered from any source distribution.
*/

#include <Box2D/Common/b2Math.h>

const b2Vec2 b2Vec2_zero(0.0f, 0.0f);
const b2Mat22 b2Mat22_identity(1.0f, 0.0f, 0.0f, 1.0f);
const b2Transform b2Transform_identity(b2Vec2_zero, b2Mat22_identity);

/// Solve A * x = b, where b is a column vector. This is more efficient
/// than computing the inverse in one-shot cases.
b2Vec3 b2Mat33::Solve33(const b2Vec3& b) const
{
	// Same term order as the retail 1.6.2 exe (FUN_004dc990 @ 0x4dc990), so the
	// float rounding matches.
	float32 det = (col3.y * col2.x - col2.y * col3.x) * col1.z +
				  (col2.z * col3.x - col2.x * col3.z) * col1.y +
				  col1.x * (col2.y * col3.z - col2.z * col3.y);
	if (det != 0.0f)
	{
		det = 1.0f / det;
	}
	b2Vec3 x;
	x.x = (b.z * (col3.y * col2.x - col2.y * col3.x) +
		   b.x * (col2.y * col3.z - col2.z * col3.y) +
		   b.y * (col2.z * col3.x - col2.x * col3.z)) * det;
	x.y = ((col3.y * b.x - b.y * col3.x) * col1.z +
		   (b.z * col3.x - b.x * col3.z) * col1.y +
		   (b.y * col3.z - col3.y * b.z) * col1.x) * det;
	x.z = det * ((b.y * col2.x - col2.y * b.x) * col1.z +
				 (col2.z * b.x - b.z * col2.x) * col1.y +
				 (col2.y * b.z - col2.z * b.y) * col1.x);
	return x;
}

/// Solve A * x = b, where b is a column vector. This is more efficient
/// than computing the inverse in one-shot cases.
b2Vec2 b2Mat33::Solve22(const b2Vec2& b) const
{
	float32 a11 = col1.x, a12 = col2.x, a21 = col1.y, a22 = col2.y;
	float32 det = a11 * a22 - a12 * a21;
	if (det != 0.0f)
	{
		det = 1.0f / det;
	}
	b2Vec2 x;
	x.x = det * (a22 * b.x - a12 * b.y);
	x.y = det * (a11 * b.y - a21 * b.x);
	return x;
}
