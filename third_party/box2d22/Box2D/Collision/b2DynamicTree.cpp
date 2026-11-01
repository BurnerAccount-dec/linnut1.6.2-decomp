/*
* Copyright (c) 2009 Erin Catto http://www.box2d.org
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

#include <Box2D/Collision/b2DynamicTree.h>
#include <cstring>
#include <cfloat>
using namespace std;


b2DynamicTree::b2DynamicTree()
{
	m_root = b2_nullNode;

	m_nodeCapacity = 16;
	m_nodeCount = 0;
	m_nodes = (b2TreeNode*)b2Alloc(m_nodeCapacity * sizeof(b2TreeNode));
	memset(m_nodes, 0, m_nodeCapacity * sizeof(b2TreeNode));

	// Build a linked list for the free list.
	for (int32 i = 0; i < m_nodeCapacity - 1; ++i)
	{
		m_nodes[i].next = i + 1;
		}
	m_nodes[m_nodeCapacity-1].next = b2_nullNode;
	m_freeList = 0;

	m_path = 0;

	m_insertionCount = 0;
}

b2DynamicTree::~b2DynamicTree()
{
	// This frees the entire tree in one shot.
	b2Free(m_nodes);
}

// Allocate a node from the pool. Grow the pool if necessary.
int32 b2DynamicTree::AllocateNode()
{
	// Expand the node pool as needed.
	if (m_freeList == b2_nullNode)
	{
		b2Assert(m_nodeCount == m_nodeCapacity);

		// The free list is empty. Rebuild a bigger pool.
		b2TreeNode* oldNodes = m_nodes;
		m_nodeCapacity *= 2;
		m_nodes = (b2TreeNode*)b2Alloc(m_nodeCapacity * sizeof(b2TreeNode));
		memcpy(m_nodes, oldNodes, m_nodeCount * sizeof(b2TreeNode));
		b2Free(oldNodes);

		// Build a linked list for the free list. The parent
		// pointer becomes the "next" pointer.
		for (int32 i = m_nodeCount; i < m_nodeCapacity - 1; ++i)
		{
			m_nodes[i].next = i + 1;
				}
		m_nodes[m_nodeCapacity-1].next = b2_nullNode;
			m_freeList = m_nodeCount;
	}

	// Peel a node off the free list.
	int32 nodeId = m_freeList;
	m_freeList = m_nodes[nodeId].next;
	m_nodes[nodeId].parent = b2_nullNode;
	m_nodes[nodeId].child1 = b2_nullNode;
	m_nodes[nodeId].child2 = b2_nullNode;
	m_nodes[nodeId].leafCount = 0;
	m_nodes[nodeId].userData = NULL;
	++m_nodeCount;
	return nodeId;
}

// Return a node to the pool.
void b2DynamicTree::FreeNode(int32 nodeId)
{
	b2Assert(0 <= nodeId && nodeId < m_nodeCapacity);
	b2Assert(0 < m_nodeCount);
	m_nodes[nodeId].next = m_freeList;
	m_freeList = nodeId;
	--m_nodeCount;
}

// Create a proxy in the tree as a leaf node. We return the index
// of the node instead of a pointer so that we can grow
// the node pool.
int32 b2DynamicTree::CreateProxy(const b2AABB& aabb, void* userData)
{
	int32 proxyId = AllocateNode();

	// Fatten the aabb.
	b2Vec2 r(b2_aabbExtension, b2_aabbExtension);
	m_nodes[proxyId].aabb.lowerBound = aabb.lowerBound - r;
	m_nodes[proxyId].aabb.upperBound = aabb.upperBound + r;
	m_nodes[proxyId].userData = userData;
	m_nodes[proxyId].leafCount = b2_spaceBuild ? 0 : 1;   // Space's tree stores the node height here (a leaf is 0), Rio's counts leaves

	InsertLeaf(proxyId);

	return proxyId;
}

void b2DynamicTree::DestroyProxy(int32 proxyId)
{
	b2Assert(0 <= proxyId && proxyId < m_nodeCapacity);
	b2Assert(m_nodes[proxyId].IsLeaf());

	RemoveLeaf(proxyId);
	FreeNode(proxyId);
}

bool b2DynamicTree::MoveProxy(int32 proxyId, const b2AABB& aabb, const b2Vec2& displacement)
{
	b2Assert(0 <= proxyId && proxyId < m_nodeCapacity);

	b2Assert(m_nodes[proxyId].IsLeaf());

	if (m_nodes[proxyId].aabb.Contains(aabb))
	{
		return false;
	}

	RemoveLeaf(proxyId);

	// Extend AABB.
	b2AABB b = aabb;
	b2Vec2 r(b2_aabbExtension, b2_aabbExtension);
	b.lowerBound = b.lowerBound - r;
	b.upperBound = b.upperBound + r;

	// Predict AABB displacement.
	b2Vec2 d = b2_aabbMultiplier * displacement;

	if (d.x < 0.0f)
	{
		b.lowerBound.x += d.x;
	}
	else
	{
		b.upperBound.x += d.x;
	}

	if (d.y < 0.0f)
	{
		b.lowerBound.y += d.y;
	}
	else
	{
		b.upperBound.y += d.y;
	}

	m_nodes[proxyId].aabb = b;

	InsertLeaf(proxyId);
	return true;
}

// AngryBirdsRio.exe's tree (Box2D 2.2.0): InsertLeaf 0x4f1e00, RemoveLeaf 0x4f2580, Rebalance 0x4f2730. The AABBs are widened on the
// way down (so there is no walk back up), every node counts its leaves, and the tree is kept in shape by Rebalance(4) after
// each pair update instead of rotations.
static inline float32 ab_perimeter(float32 lx, float32 ly, float32 ux, float32 uy)
{
	return ((ux - lx) + (uy - ly)) * 2.0f;
}

void b2DynamicTree::InsertLeaf(int32 leaf)
{
	if (b2_spaceBuild) { InsertLeafSpace(leaf); return; }
	++m_insertionCount;

	if (m_root == b2_nullNode)
	{
		m_root = leaf;
		m_nodes[m_root].parent = b2_nullNode;
		return;
	}

	// Find the best sibling for this node
	const float32 llx = m_nodes[leaf].aabb.lowerBound.x, lly = m_nodes[leaf].aabb.lowerBound.y;
	const float32 lux = m_nodes[leaf].aabb.upperBound.x, luy = m_nodes[leaf].aabb.upperBound.y;
	int32 sibling = m_root;
	while (m_nodes[sibling].IsLeaf() == false)
	{
		int32 child1 = m_nodes[sibling].child1;
		int32 child2 = m_nodes[sibling].child2;
		b2TreeNode* node = m_nodes + sibling;

		// Expand the node's AABB.
		node->aabb.lowerBound.x = b2Min(node->aabb.lowerBound.x, llx);
		node->aabb.lowerBound.y = b2Min(node->aabb.lowerBound.y, lly);
		node->aabb.upperBound.x = b2Max(node->aabb.upperBound.x, lux);
		node->aabb.upperBound.y = b2Max(node->aabb.upperBound.y, luy);
		node->leafCount += 1;

		float32 siblingArea = ab_perimeter(node->aabb.lowerBound.x, node->aabb.lowerBound.y, node->aabb.upperBound.x, node->aabb.upperBound.y);
		float32 parentArea = ab_perimeter(b2Min(node->aabb.lowerBound.x, llx), b2Min(node->aabb.lowerBound.y, lly),
										  b2Max(node->aabb.upperBound.x, lux), b2Max(node->aabb.upperBound.y, luy));
		float32 cost = parentArea * 2.0f;
		float32 inheritanceCost = (parentArea - siblingArea) * 2.0f;

		float32 cost1, cost2;
		{
			const b2AABB& a = m_nodes[child1].aabb;
			float32 newArea = ab_perimeter(b2Min(a.lowerBound.x, llx), b2Min(a.lowerBound.y, lly), b2Max(a.upperBound.x, lux), b2Max(a.upperBound.y, luy));
			if (m_nodes[child1].IsLeaf())
			{
				cost1 = inheritanceCost + newArea;
			}
			else
			{
				float32 half = (a.upperBound.y - a.lowerBound.y) + (a.upperBound.x - a.lowerBound.x);
				cost1 = inheritanceCost + (newArea - (half + half));
			}
		}
		{
			const b2AABB& a = m_nodes[child2].aabb;
			float32 newArea = ab_perimeter(b2Min(a.lowerBound.x, llx), b2Min(a.lowerBound.y, lly), b2Max(a.upperBound.x, lux), b2Max(a.upperBound.y, luy));
			if (m_nodes[child2].IsLeaf())
			{
				cost2 = newArea + inheritanceCost;
			}
			else
			{
				float32 half = (a.upperBound.y - a.lowerBound.y) + (a.upperBound.x - a.lowerBound.x);
				cost2 = (newArea - (half + half)) + inheritanceCost;
			}
		}

		// Descend according to the minimum cost.
		if (cost < cost1 && cost < cost2)
		{
			break;
		}

		sibling = (cost1 < cost2) ? child1 : child2;
	}

	// Create a new parent.
	int32 oldParent = m_nodes[sibling].parent;
	int32 newParent = AllocateNode();
	m_nodes[newParent].parent = oldParent;
	m_nodes[newParent].userData = NULL;
	m_nodes[newParent].aabb.lowerBound.x = b2Min(m_nodes[sibling].aabb.lowerBound.x, llx);
	m_nodes[newParent].aabb.lowerBound.y = b2Min(m_nodes[sibling].aabb.lowerBound.y, lly);
	m_nodes[newParent].aabb.upperBound = b2Max(b2Vec2(lux, luy), m_nodes[sibling].aabb.upperBound);
	m_nodes[newParent].leafCount = m_nodes[sibling].leafCount + 1;

	if (oldParent != b2_nullNode)
	{
		// The sibling was not the root.
		if (m_nodes[oldParent].child1 == sibling)
		{
			m_nodes[oldParent].child1 = newParent;
		}
		else
		{
			m_nodes[oldParent].child2 = newParent;
		}
	}
	else
	{
		m_root = newParent;
	}

	m_nodes[newParent].child1 = sibling;
	m_nodes[newParent].child2 = leaf;
	m_nodes[sibling].parent = newParent;
	m_nodes[leaf].parent = newParent;
}

void b2DynamicTree::RemoveLeaf(int32 leaf)
{
	if (b2_spaceBuild) { RemoveLeafSpace(leaf); return; }
	if (leaf == m_root)
	{
		m_root = b2_nullNode;
		return;
	}

	int32 parent = m_nodes[leaf].parent;
	int32 grandParent = m_nodes[parent].parent;
	int32 sibling = (m_nodes[parent].child1 == leaf) ? m_nodes[parent].child2 : m_nodes[parent].child1;

	if (grandParent != b2_nullNode)
	{
		// Destroy parent and connect sibling to grandParent.
		if (m_nodes[grandParent].child1 == parent)
		{
			m_nodes[grandParent].child1 = sibling;
		}
		else
		{
			m_nodes[grandParent].child2 = sibling;
		}
		m_nodes[sibling].parent = grandParent;
		FreeNode(parent);

		// Adjust ancestor bounds.
		int32 index = grandParent;
		while (index != b2_nullNode)
		{
			b2TreeNode* node = m_nodes + index;
			const b2TreeNode* c1 = m_nodes + node->child1;
			const b2TreeNode* c2 = m_nodes + node->child2;
			node->aabb.lowerBound.x = b2Min(c2->aabb.lowerBound.x, c1->aabb.lowerBound.x);
			node->aabb.lowerBound.y = b2Min(c1->aabb.lowerBound.y, c2->aabb.lowerBound.y);
			node->aabb.upperBound.x = b2Max(c2->aabb.upperBound.x, c1->aabb.upperBound.x);
			node->aabb.upperBound.y = b2Max(c2->aabb.upperBound.y, c1->aabb.upperBound.y);
			node->leafCount -= 1;
			index = node->parent;
		}
	}
	else
	{
		m_root = sibling;
		m_nodes[sibling].parent = b2_nullNode;
		FreeNode(parent);
	}
}

// Re-inserts one leaf per iteration, walking down the tree along the bits of m_path.
void b2DynamicTree::Rebalance(int32 iterations)
{
	if (b2_spaceBuild || m_root == b2_nullNode)
	{
		return;
	}

	for (int32 i = 0; i < iterations; ++i)
	{
		int32 node = m_root;
		uint32 bit = 0;
		while (m_nodes[node].IsLeaf() == false)
		{
			const b2TreeNode* n = m_nodes + node;
			node = ((m_path >> bit) & 1) ? n->child2 : n->child1;
			bit = (bit + 1) & 31;
		}
		++m_path;

		RemoveLeaf(node);
		InsertLeaf(node);
	}
}

// AngryBirdsSpace.exe's tree (the Box2D 2.3 one): InsertLeaf 0x4a3220, RemoveLeaf 0x4a3940, Balance 0x4a2fd0 (AVL-style rotations), MoveProxy 0x4a3bf0.
// The sibling is picked by the surface-area heuristic, the AABBs are recombined on the way back up, and every node stores its height.
static inline float32 sp_perimeter(const b2AABB& a)
{
	float32 wx = a.upperBound.x - a.lowerBound.x;
	float32 wy = a.upperBound.y - a.lowerBound.y;
	return 2.0f * (wx + wy);
}

static inline b2AABB sp_combine(const b2AABB& a, const b2AABB& b)
{
	b2AABB c;
	c.Combine(a, b);
	return c;
}

void b2DynamicTree::InsertLeafSpace(int32 leaf)
{
	++m_insertionCount;

	if (m_root == b2_nullNode)
	{
		m_root = leaf;
		m_nodes[m_root].parent = b2_nullNode;
		return;
	}

	// Find the best sibling for this node
	b2AABB leafAABB = m_nodes[leaf].aabb;
	int32 index = m_root;
	while (m_nodes[index].IsLeaf() == false)
	{
		int32 child1 = m_nodes[index].child1;
		int32 child2 = m_nodes[index].child2;

		float32 area = sp_perimeter(m_nodes[index].aabb);
		b2AABB combinedAABB = sp_combine(m_nodes[index].aabb, leafAABB);
		float32 combinedArea = sp_perimeter(combinedAABB);

		// Cost of creating a new parent for this node and the new leaf
		float32 cost = 2.0f * combinedArea;

		// Minimum cost of pushing the leaf further down the tree
		float32 inheritanceCost = 2.0f * (combinedArea - area);

		// Cost of descending into child1
		float32 cost1;
		if (m_nodes[child1].IsLeaf())
		{
			cost1 = sp_perimeter(sp_combine(leafAABB, m_nodes[child1].aabb)) + inheritanceCost;
		}
		else
		{
			b2AABB aabb = sp_combine(leafAABB, m_nodes[child1].aabb);
			float32 oldArea = sp_perimeter(m_nodes[child1].aabb);
			float32 newArea = sp_perimeter(aabb);
			cost1 = (newArea - oldArea) + inheritanceCost;
		}

		// Cost of descending into child2
		float32 cost2;
		if (m_nodes[child2].IsLeaf())
		{
			cost2 = sp_perimeter(sp_combine(leafAABB, m_nodes[child2].aabb)) + inheritanceCost;
		}
		else
		{
			b2AABB aabb = sp_combine(leafAABB, m_nodes[child2].aabb);
			float32 oldArea = sp_perimeter(m_nodes[child2].aabb);
			float32 newArea = sp_perimeter(aabb);
			cost2 = (newArea - oldArea) + inheritanceCost;
		}

		// Descend according to the minimum cost.
		if (cost < cost1 && cost < cost2)
		{
			break;
		}

		// Descend
		index = (cost1 < cost2) ? child1 : child2;
	}

	int32 sibling = index;

	// Create a new parent.
	int32 oldParent = m_nodes[sibling].parent;
	int32 newParent = AllocateNode();
	m_nodes[newParent].parent = oldParent;
	m_nodes[newParent].userData = NULL;
	m_nodes[newParent].aabb = sp_combine(leafAABB, m_nodes[sibling].aabb);
	m_nodes[newParent].leafCount = m_nodes[sibling].leafCount + 1;

	if (oldParent != b2_nullNode)
	{
		// The sibling was not the root.
		if (m_nodes[oldParent].child1 == sibling)
		{
			m_nodes[oldParent].child1 = newParent;
		}
		else
		{
			m_nodes[oldParent].child2 = newParent;
		}

		m_nodes[newParent].child1 = sibling;
		m_nodes[newParent].child2 = leaf;
		m_nodes[sibling].parent = newParent;
		m_nodes[leaf].parent = newParent;
	}
	else
	{
		// The sibling was the root.
		m_nodes[newParent].child1 = sibling;
		m_nodes[newParent].child2 = leaf;
		m_nodes[sibling].parent = newParent;
		m_nodes[leaf].parent = newParent;
		m_root = newParent;
	}

	// Walk back up the tree fixing heights and AABBs
	index = m_nodes[leaf].parent;
	while (index != b2_nullNode)
	{
		index = BalanceSpace(index);

		int32 child1 = m_nodes[index].child1;
		int32 child2 = m_nodes[index].child2;

		m_nodes[index].leafCount = 1 + b2Max(m_nodes[child1].leafCount, m_nodes[child2].leafCount);
		m_nodes[index].aabb = sp_combine(m_nodes[child1].aabb, m_nodes[child2].aabb);

		index = m_nodes[index].parent;
	}
}

void b2DynamicTree::RemoveLeafSpace(int32 leaf)
{
	if (leaf == m_root)
	{
		m_root = b2_nullNode;
		return;
	}

	int32 parent = m_nodes[leaf].parent;
	int32 grandParent = m_nodes[parent].parent;
	int32 sibling = (m_nodes[parent].child1 == leaf) ? m_nodes[parent].child2 : m_nodes[parent].child1;

	if (grandParent != b2_nullNode)
	{
		// Destroy parent and connect sibling to grandParent.
		if (m_nodes[grandParent].child1 == parent)
		{
			m_nodes[grandParent].child1 = sibling;
		}
		else
		{
			m_nodes[grandParent].child2 = sibling;
		}
		m_nodes[sibling].parent = grandParent;
		FreeNode(parent);

		// Adjust ancestor bounds.
		int32 index = grandParent;
		while (index != b2_nullNode)
		{
			index = BalanceSpace(index);

			int32 child1 = m_nodes[index].child1;
			int32 child2 = m_nodes[index].child2;

			m_nodes[index].aabb = sp_combine(m_nodes[child1].aabb, m_nodes[child2].aabb);
			m_nodes[index].leafCount = 1 + b2Max(m_nodes[child1].leafCount, m_nodes[child2].leafCount);

			index = m_nodes[index].parent;
		}
	}
	else
	{
		m_root = sibling;
		m_nodes[sibling].parent = b2_nullNode;
		FreeNode(parent);
	}
}

// Perform a left or right rotation if node A is imbalanced (0x4a2fd0). Returns the new root index.
int32 b2DynamicTree::BalanceSpace(int32 iA)
{
	b2TreeNode* A = m_nodes + iA;
	if (A->IsLeaf() || A->leafCount < 2)
	{
		return iA;
	}

	int32 iB = A->child1;
	int32 iC = A->child2;
	b2TreeNode* B = m_nodes + iB;
	b2TreeNode* C = m_nodes + iC;

	int32 balance = C->leafCount - B->leafCount;

	// Rotate C up
	if (balance > 1)
	{
		int32 iF = C->child1;
		int32 iG = C->child2;
		b2TreeNode* F = m_nodes + iF;
		b2TreeNode* G = m_nodes + iG;

		// Swap A and C
		C->child1 = iA;
		C->parent = A->parent;
		A->parent = iC;

		// A's old parent should point to C
		if (C->parent != b2_nullNode)
		{
			if (m_nodes[C->parent].child1 == iA)
			{
				m_nodes[C->parent].child1 = iC;
			}
			else
			{
				m_nodes[C->parent].child2 = iC;
			}
		}
		else
		{
			m_root = iC;
		}

		// Rotate
		if (F->leafCount > G->leafCount)
		{
			C->child2 = iF;
			A->child2 = iG;
			G->parent = iA;
			A->aabb = sp_combine(B->aabb, G->aabb);
			C->aabb = sp_combine(A->aabb, F->aabb);

			A->leafCount = 1 + b2Max(B->leafCount, G->leafCount);
			C->leafCount = 1 + b2Max(A->leafCount, F->leafCount);
		}
		else
		{
			C->child2 = iG;
			A->child2 = iF;
			F->parent = iA;
			A->aabb = sp_combine(B->aabb, F->aabb);
			C->aabb = sp_combine(A->aabb, G->aabb);

			A->leafCount = 1 + b2Max(B->leafCount, F->leafCount);
			C->leafCount = 1 + b2Max(A->leafCount, G->leafCount);
		}

		return iC;
	}

	// Rotate B up
	if (balance < -1)
	{
		int32 iD = B->child1;
		int32 iE = B->child2;
		b2TreeNode* D = m_nodes + iD;
		b2TreeNode* E = m_nodes + iE;

		// Swap A and B
		B->child1 = iA;
		B->parent = A->parent;
		A->parent = iB;

		// A's old parent should point to B
		if (B->parent != b2_nullNode)
		{
			if (m_nodes[B->parent].child1 == iA)
			{
				m_nodes[B->parent].child1 = iB;
			}
			else
			{
				m_nodes[B->parent].child2 = iB;
			}
		}
		else
		{
			m_root = iB;
		}

		// Rotate
		if (D->leafCount > E->leafCount)
		{
			B->child2 = iD;
			A->child1 = iE;
			E->parent = iA;
			A->aabb = sp_combine(C->aabb, E->aabb);
			B->aabb = sp_combine(A->aabb, D->aabb);

			A->leafCount = 1 + b2Max(C->leafCount, E->leafCount);
			B->leafCount = 1 + b2Max(A->leafCount, D->leafCount);
		}
		else
		{
			B->child2 = iE;
			A->child1 = iD;
			D->parent = iA;
			A->aabb = sp_combine(C->aabb, D->aabb);
			B->aabb = sp_combine(A->aabb, E->aabb);

			A->leafCount = 1 + b2Max(C->leafCount, D->leafCount);
			B->leafCount = 1 + b2Max(A->leafCount, E->leafCount);
		}

		return iB;
	}

	return iA;
}

int32 b2DynamicTree::ComputeHeight(int32 nodeId) const
{
	if (nodeId == b2_nullNode)
	{
		return 0;
	}
	const b2TreeNode* node = m_nodes + nodeId;
	if (node->IsLeaf())
	{
		return 0;
	}
	return 1 + b2Max(ComputeHeight(node->child1), ComputeHeight(node->child2));
}

int32 b2DynamicTree::GetHeight() const
{
	return ComputeHeight(m_root);
}

int32 b2DynamicTree::GetMaxBalance() const
{
	return 0;
}

float32 b2DynamicTree::GetAreaRatio() const
{
	return 0.0f;
}
