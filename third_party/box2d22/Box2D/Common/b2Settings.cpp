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

// Angry Birds Rio: the polygon skin is the stock two linearSlops (0.1), which makes blocks 0.1-0.2 apart count as touching (Smuggler's Plane 12-15
// relies on that for its TNT). b2_solverSkinFraction = 0 keeps the position solver from pushing flush-placed stacks apart by the skin
// (with the full skin Smuggler's Plane 12-8 and Mauro's ice tower collapse): the skin only decides when bodies touch. Scripts can change the
// skin with setPolygonRadius().
float b2_polygonRadiusValue = 2.0f * b2_linearSlop;
float b2_solverSkinFraction = 0.0f;

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
