/*
* Copyright (c) 2007-2009 Erin Catto http://www.box2d.org
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

/// Solve A * x = b, where b is a column vector. This is more efficient
/// than computing the inverse in one-shot cases.
b2Vec3 b2Mat33::Solve33(const b2Vec3& b) const
{
	// Same term order as AngryBirdsRio.exe (0x4e8930), so the float rounding matches.
	float32 det = (ez.y * ey.x - ey.y * ez.x) * ex.z +
				  (ey.z * ez.x - ey.x * ez.z) * ex.y +
				  ex.x * (ey.y * ez.z - ey.z * ez.y);
	if (det != 0.0f)
	{
		det = 1.0f / det;
	}
	b2Vec3 x;
	x.x = (b.z * (ez.y * ey.x - ey.y * ez.x) +
		   b.x * (ey.y * ez.z - ey.z * ez.y) +
		   b.y * (ey.z * ez.x - ey.x * ez.z)) * det;
	x.y = ((ez.y * b.x - b.y * ez.x) * ex.z +
		   (b.z * ez.x - b.x * ez.z) * ex.y +
		   (b.y * ez.z - ez.y * b.z) * ex.x) * det;
	x.z = det * ((b.y * ey.x - ey.y * b.x) * ex.z +
				 (ey.z * b.x - b.z * ey.x) * ex.y +
				 (ey.y * b.z - ey.z * b.y) * ex.x);
	return x;
}

/// Solve A * x = b, where b is a column vector. This is more efficient
/// than computing the inverse in one-shot cases.
b2Vec2 b2Mat33::Solve22(const b2Vec2& b) const
{
	float32 a11 = ex.x, a12 = ey.x, a21 = ex.y, a22 = ey.y;
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
