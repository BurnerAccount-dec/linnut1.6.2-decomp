/*
* Copyright (c) 2006-2009 Erin Catto http://www.box2d.org
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

#include <Box2D/Common/b2Settings.h>
#include <cstdlib>

// Angry Birds Rio: the polygon skin is the exe's own 0.1 (two linear slops, 0x4e7c40), and the exe's position solver subtracts both radii in full
// (0x4f2da0), so b2_solverSkinFraction is 1. (Earlier builds used 0.65 as a stand-in for the joint and solver differences that are now matched.)
// Scripts can change the skin with setPolygonRadius().
float b2_linearSlopValue = 0.05f;
float b2_timeToSleepValue = 0.25f;
float b2_linearSleepToleranceValue = 0.1f;
float b2_polygonRadiusValue = 0.1f;
float b2_solverSkinFraction = 1.0f;
bool b2_coincidentCircleNormal = true;
bool b2_spaceBuild = false;

float b2_velocityThresholdV22 = 1.0f;
float b2_baumgarteV22 = 0.2f;
float b2_maxRotationV22 = 0.5f * 3.14159265359f;
float b2_angularSleepToleranceV22 = 2.0f / 180.0f * 3.14159265359f;
float b2_maxLinearCorrectionV22 = 0.2f;
float32 b2_maxTranslation = 2.0f;
float32 b2_maxTranslationSquared = 4.0f;

b2Version b2_version = {2, 2, 0};

// Memory allocators. Modify these to use your own allocator.
void* b2Alloc(int32 size)
{
	return malloc(size);
}

void b2Free(void* mem)
{
	free(mem);
}
