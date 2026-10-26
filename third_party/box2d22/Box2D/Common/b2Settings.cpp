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

// Angry Birds Rio: the authored levels place blocks flush against each other, so the skin is one linearSlop (0.05), not the stock two.
// With 0.1 the skins of touching blocks overlap and push stacks apart (Smuggler's Plane 12-8 and the ice tower in Mauro's level collapse);
// with 0.05 they stay where the level put them, like in the retail exe. Scripts can change it with setPolygonRadius().
float b2_polygonRadiusValue = b2_linearSlop;

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
