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

#include <Box2D/Collision/b2Collision.h>
#include <Box2D/Collision/b2Distance.h>
#include <Box2D/Collision/b2TimeOfImpact.h>
#include <Box2D/Collision/Shapes/b2CircleShape.h>
#include <Box2D/Collision/Shapes/b2PolygonShape.h>

#include <cstdio>
#include <cstdlib>

int32 b2_toiCalls, b2_toiIters, b2_toiMaxIters;
int32 b2_toiRootIters, b2_toiMaxRootIters;

int32 b2_toiMaxOptIters;

struct b2SeparationFunction
{
	enum Type
	{
		e_points,
		e_faceA,
		e_faceB
	};

	// TODO_ERIN might not need to return the separation

	float32 Initialize(const b2SimplexCache* cache,
		const b2DistanceProxy* proxyA, const b2Sweep& sweepA,
		const b2DistanceProxy* proxyB, const b2Sweep& sweepB)
	{
		m_proxyA = proxyA;
		m_proxyB = proxyB;
		int32 count = cache->count;
		b2Assert(0 < count && count < 3);

		m_sweepA = sweepA;
		m_sweepB = sweepB;

		b2Transform xfA, xfB;
		m_sweepA.GetTransform(&xfA, 0.0f);
		m_sweepB.GetTransform(&xfB, 0.0f);

		if (count == 1)
		{
			m_type = e_points;
			b2Vec2 localPointA = m_proxyA->GetVertex(cache->indexA[0]);
			b2Vec2 localPointB = m_proxyB->GetVertex(cache->indexB[0]);
			b2Vec2 pointA = b2Mul(xfA, localPointA);
			b2Vec2 pointB = b2Mul(xfB, localPointB);
			m_axis = pointB - pointA;
			float32 s = m_axis.Normalize();
			return s;
		}
		else if (cache->indexA[0] == cache->indexA[1])
		{
			// Two points on B and one on A.
			m_type = e_faceB;
			b2Vec2 localPointB1 = proxyB->GetVertex(cache->indexB[0]);
			b2Vec2 localPointB2 = proxyB->GetVertex(cache->indexB[1]);

			m_axis = b2Cross(localPointB2 - localPointB1, 1.0f);
			m_axis.Normalize();
			b2Vec2 normal = b2Mul(xfB.R, m_axis);

			m_localPoint = 0.5f * (localPointB1 + localPointB2);
			b2Vec2 pointB = b2Mul(xfB, m_localPoint);

			b2Vec2 localPointA = proxyA->GetVertex(cache->indexA[0]);
			b2Vec2 pointA = b2Mul(xfA, localPointA);

			float32 s = b2Dot(pointA - pointB, normal);
			if (s < 0.0f)
			{
				m_axis = -m_axis;
				s = -s;
			}
			return s;
		}
		else
		{
			// Two points on A and one or two points on B.
			m_type = e_faceA;
			b2Vec2 localPointA1 = m_proxyA->GetVertex(cache->indexA[0]);
			b2Vec2 localPointA2 = m_proxyA->GetVertex(cache->indexA[1]);
			
			m_axis = b2Cross(localPointA2 - localPointA1, 1.0f);
			m_axis.Normalize();
			b2Vec2 normal = b2Mul(xfA.R, m_axis);

			m_localPoint = 0.5f * (localPointA1 + localPointA2);
			b2Vec2 pointA = b2Mul(xfA, m_localPoint);

			b2Vec2 localPointB = m_proxyB->GetVertex(cache->indexB[0]);
			b2Vec2 pointB = b2Mul(xfB, localPointB);

			float32 s = b2Dot(pointB - pointA, normal);
			if (s < 0.0f)
			{
				m_axis = -m_axis;
				s = -s;
			}
			return s;
		}
	}

	float32 FindMinSeparation(int32* indexA, int32* indexB, float32 t) const
	{
		b2Transform xfA, xfB;
		m_sweepA.GetTransform(&xfA, t);
		m_sweepB.GetTransform(&xfB, t);

		switch (m_type)
		{
		case e_points:
			{
				b2Vec2 axisA = b2MulT(xfA.R,  m_axis);
				b2Vec2 axisB = b2MulT(xfB.R, -m_axis);

				*indexA = m_proxyA->GetSupport(axisA);
				*indexB = m_proxyB->GetSupport(axisB);

				b2Vec2 localPointA = m_proxyA->GetVertex(*indexA);
				b2Vec2 localPointB = m_proxyB->GetVertex(*indexB);
				
				b2Vec2 pointA = b2Mul(xfA, localPointA);
				b2Vec2 pointB = b2Mul(xfB, localPointB);

				float32 separation = b2Dot(pointB - pointA, m_axis);
				return separation;
			}

		case e_faceA:
			{
				b2Vec2 normal = b2Mul(xfA.R, m_axis);
				b2Vec2 pointA = b2Mul(xfA, m_localPoint);

				b2Vec2 axisB = b2MulT(xfB.R, -normal);
				
				*indexA = -1;
				*indexB = m_proxyB->GetSupport(axisB);

				b2Vec2 localPointB = m_proxyB->GetVertex(*indexB);
				b2Vec2 pointB = b2Mul(xfB, localPointB);

				float32 separation = b2Dot(pointB - pointA, normal);
				return separation;
			}

		case e_faceB:
			{
				b2Vec2 normal = b2Mul(xfB.R, m_axis);
				b2Vec2 pointB = b2Mul(xfB, m_localPoint);

				b2Vec2 axisA = b2MulT(xfA.R, -normal);

				*indexB = -1;
				*indexA = m_proxyA->GetSupport(axisA);

				b2Vec2 localPointA = m_proxyA->GetVertex(*indexA);
				b2Vec2 pointA = b2Mul(xfA, localPointA);

				float32 separation = b2Dot(pointA - pointB, normal);
				return separation;
			}

		default:
			b2Assert(false);
			*indexA = -1;
			*indexB = -1;
			return 0.0f;
		}
	}

	float32 Evaluate(int32 indexA, int32 indexB, float32 t) const
	{
		b2Transform xfA, xfB;
		m_sweepA.GetTransform(&xfA, t);
		m_sweepB.GetTransform(&xfB, t);

		switch (m_type)
		{
		case e_points:
			{
				b2Vec2 axisA = b2MulT(xfA.R,  m_axis);
				b2Vec2 axisB = b2MulT(xfB.R, -m_axis);

				b2Vec2 localPointA = m_proxyA->GetVertex(indexA);
				b2Vec2 localPointB = m_proxyB->GetVertex(indexB);

				b2Vec2 pointA = b2Mul(xfA, localPointA);
				b2Vec2 pointB = b2Mul(xfB, localPointB);
				float32 separation = b2Dot(pointB - pointA, m_axis);

				return separation;
			}

		case e_faceA:
			{
				b2Vec2 normal = b2Mul(xfA.R, m_axis);
				b2Vec2 pointA = b2Mul(xfA, m_localPoint);

				b2Vec2 axisB = b2MulT(xfB.R, -normal);

				b2Vec2 localPointB = m_proxyB->GetVertex(indexB);
				b2Vec2 pointB = b2Mul(xfB, localPointB);

				float32 separation = b2Dot(pointB - pointA, normal);
				return separation;
			}

		case e_faceB:
			{
				b2Vec2 normal = b2Mul(xfB.R, m_axis);
				b2Vec2 pointB = b2Mul(xfB, m_localPoint);

				b2Vec2 axisA = b2MulT(xfA.R, -normal);

				b2Vec2 localPointA = m_proxyA->GetVertex(indexA);
				b2Vec2 pointA = b2Mul(xfA, localPointA);

				float32 separation = b2Dot(pointA - pointB, normal);
				return separation;
			}

		default:
			b2Assert(false);
			return 0.0f;
		}
	}

	const b2DistanceProxy* m_proxyA;
	const b2DistanceProxy* m_proxyB;
	b2Sweep m_sweepA, m_sweepB;
	Type m_type;
	b2Vec2 m_localPoint;
	b2Vec2 m_axis;
};

// CCD via the local separating axis method. This seeks progression
// by computing the largest time at which separation is maintained.
// ---------------------------------------------------------------------------
// AngryBirds.exe 1.6.2 b2TimeOfImpact (0x4e70d0), separation function constructor (0x4e67b0) and
// evaluator (0x4e6480). This is the Box2D 2.1.0 conservative advancement with a secant/bisection
// root finder, called by Contact::ComputeTOI (0x4e0330) with tolerance 0.05.
// ---------------------------------------------------------------------------
namespace
{
const float32 kExeToiTolerance = 0.05f;

struct b2ExeSeparation
{
	const b2DistanceProxy* proxyA;
	const b2DistanceProxy* proxyB;
	int32 type; // 0 points, 1 faceA, 2 faceB
	b2Vec2 localPoint;
	b2Vec2 axis;

	void Initialize(const b2SimplexCache* cache, const b2DistanceProxy* pA, const b2Transform& xfA,
					const b2DistanceProxy* pB, const b2Transform& xfB)
	{
		proxyA = pA;
		proxyB = pB;
		localPoint.SetZero();
		if (cache->count == 1)
		{
			type = 0;
			b2Vec2 a = b2Mul(xfA, pA->GetVertex(cache->indexA[0]));
			b2Vec2 b = b2Mul(xfB, pB->GetVertex(cache->indexB[0]));
			axis = b - a;
			axis.Normalize();
			return;
		}
		if (cache->indexB[0] == cache->indexB[1])
		{
			type = 1;
			b2Vec2 a1 = pA->GetVertex(cache->indexA[0]);
			b2Vec2 a2 = pA->GetVertex(cache->indexA[1]);
			b2Vec2 b1 = pB->GetVertex(cache->indexB[0]);
			localPoint = 0.5f * (a1 + a2);
			axis = b2Cross(a2 - a1, 1.0f);
			axis.Normalize();
			b2Vec2 normal = b2Mul(xfA.R, axis);
			b2Vec2 pa = b2Mul(xfA, localPoint);
			b2Vec2 pb = b2Mul(xfB, b1);
			if (b2Dot(pb - pa, normal) < 0.0f)
			{
				axis = -axis;
			}
			return;
		}
		if (cache->indexA[0] == cache->indexA[1])
		{
			type = 2;
			b2Vec2 a1 = pA->GetVertex(cache->indexA[0]);
			b2Vec2 b1 = pB->GetVertex(cache->indexB[0]);
			b2Vec2 b2v = pB->GetVertex(cache->indexB[1]);
			localPoint = 0.5f * (b1 + b2v);
			axis = b2Cross(b2v - b1, 1.0f);
			axis.Normalize();
			b2Vec2 normal = b2Mul(xfB.R, axis);
			b2Vec2 pb = b2Mul(xfB, localPoint);
			b2Vec2 pa = b2Mul(xfA, a1);
			if (b2Dot(pa - pb, normal) < 0.0f)
			{
				axis = -axis;
			}
			return;
		}

		// Two edges: closest points between the two segments.
		b2Vec2 p1l = pA->GetVertex(cache->indexA[0]);
		b2Vec2 p2l = pA->GetVertex(cache->indexA[1]);
		b2Vec2 q1l = pB->GetVertex(cache->indexB[0]);
		b2Vec2 q2l = pB->GetVertex(cache->indexB[1]);
		b2Vec2 p1 = b2Mul(xfA, p1l);
		b2Vec2 d1l = p2l - p1l;
		b2Vec2 d2l = q2l - q1l;
		b2Vec2 d1 = b2Mul(xfA.R, d1l);
		b2Vec2 d2 = b2Mul(xfB.R, d2l);
		float32 a = b2Dot(d1, d1);
		float32 e = b2Dot(d2, d2);
		b2Vec2 r = p1 - b2Mul(xfB, q1l);
		float32 c = b2Dot(d1, r);
		float32 f = b2Dot(d2, r);
		float32 b = b2Dot(d1, d2);
		float32 denom = a * e - b * b;
		float32 s = 0.0f;
		if (denom != 0.0f)
		{
			s = b2Clamp((b * f - c * e) / denom, 0.0f, 1.0f);
		}
		float32 t = (s * b + f) / e;
		if (t < 0.0f)
		{
			t = 0.0f;
			s = b2Clamp(-c / a, 0.0f, 1.0f);
		}
		else if (t > 1.0f)
		{
			t = 1.0f;
			s = b2Clamp((b - c) / a, 0.0f, 1.0f);
		}
		b2Vec2 closestA = p1l + s * d1l;
		b2Vec2 closestB = q1l + t * d2l;
		if (s == 0.0f || s == 1.0f)
		{
			type = 2;
			axis.Set(d2l.y, -d2l.x);
			axis.Normalize();
			localPoint = closestB;
			b2Vec2 normal = b2Mul(xfB.R, axis);
			b2Vec2 pa = b2Mul(xfA, closestA);
			b2Vec2 pb = b2Mul(xfB, closestB);
			if (b2Dot(pa - pb, normal) < 0.0f)
			{
				axis = -axis;
			}
		}
		else
		{
			type = 1;
			axis.Set(d1l.y, -d1l.x);
			axis.Normalize();
			localPoint = closestA;
			b2Vec2 normal = b2Mul(xfA.R, axis);
			b2Vec2 pa = b2Mul(xfA, closestA);
			b2Vec2 pb = b2Mul(xfB, closestB);
			if (b2Dot(pb - pa, normal) < 0.0f)
			{
				axis = -axis;
			}
		}
	}

	float32 Evaluate(const b2Transform& xfA, const b2Transform& xfB) const
	{
		if (type == 0)
		{
			b2Vec2 axisA = b2MulT(xfA.R, axis);
			b2Vec2 axisB = b2MulT(xfB.R, -axis);
			b2Vec2 pa = b2Mul(xfA, proxyA->GetSupportVertex(axisA));
			b2Vec2 pb = b2Mul(xfB, proxyB->GetSupportVertex(axisB));
			return b2Dot(pb - pa, axis);
		}
		if (type == 1)
		{
			b2Vec2 normal = b2Mul(xfA.R, axis);
			b2Vec2 pa = b2Mul(xfA, localPoint);
			b2Vec2 axisB = b2MulT(xfB.R, -normal);
			b2Vec2 pb = b2Mul(xfB, proxyB->GetSupportVertex(axisB));
			return b2Dot(pb - pa, normal);
		}
		if (type == 2)
		{
			b2Vec2 normal = b2Mul(xfB.R, axis);
			b2Vec2 pb = b2Mul(xfB, localPoint);
			b2Vec2 axisA = b2MulT(xfA.R, -normal);
			b2Vec2 pa = b2Mul(xfA, proxyA->GetSupportVertex(axisA));
			return b2Dot(pa - pb, normal);
		}
		return 0.0f;
	}
};
}

static float32 b2TimeOfImpactExe(const b2TOIInput* input)
{
	const float32 tolerance = kExeToiTolerance;
	const float32 totalRadius = input->proxyA.m_radius + input->proxyB.m_radius;

	b2DistanceInput dIn;
	dIn.proxyA = input->proxyA;
	dIn.proxyB = input->proxyB;
	dIn.useRadii = false;

	b2SimplexCache cache;
	cache.count = 0;
	b2DistanceOutput dOut;

	float32 t1 = 0.0f;
	float32 target = 0.0f;
	float32 cap = 0.0f;
	int32 iter = 0;

	input->sweepA.GetTransform(&dIn.transformA, 0.0f);
	input->sweepB.GetTransform(&dIn.transformB, 0.0f);
	b2Distance(&dOut, &cache, &dIn);

	for (;;)
	{
		if (dOut.distance <= 0.0f)
		{
			return 1.0f;
		}

		b2ExeSeparation fcn;
		fcn.Initialize(&cache, &input->proxyA, dIn.transformA, &input->proxyB, dIn.transformB);
		float32 s1 = fcn.Evaluate(dIn.transformA, dIn.transformB);
		if (s1 <= 0.0f)
		{
			return 1.0f;
		}

		if (iter == 0)
		{
			float32 base;
			if (s1 <= totalRadius)
			{
				base = totalRadius * 0.02f;
				cap = s1;
			}
			else
			{
				base = totalRadius * 0.75f;
				cap = totalRadius;
			}
			cap = cap - tolerance;
			target = base;
			if (base < cap)
			{
				target = cap;
			}
		}

		if (s1 - target < tolerance * 0.5f)
		{
			return iter == 0 ? 1.0f : t1;
		}

		float32 t2 = t1;
		float32 a1 = t1, a2 = 1.0f;
		float32 sa = s1;

		b2Transform xfA, xfB;
		input->sweepA.GetTransform(&xfA, 1.0f);
		input->sweepB.GetTransform(&xfB, 1.0f);
		float32 sb = fcn.Evaluate(xfA, xfB);
		if (target <= sb)
		{
			return 1.0f;
		}

		uint32 k = 0;
		const float32 rootTol = tolerance * 0.025f;
		do
		{
			float32 t;
			if ((k & 1) == 0)
			{
				t = (a1 + a2) * 0.5f;
			}
			else
			{
				t = a1 + ((a2 - a1) * (target - sa)) / (sb - sa);
			}
			input->sweepA.GetTransform(&xfA, t);
			input->sweepB.GetTransform(&xfB, t);
			float32 s = fcn.Evaluate(xfA, xfB);
			float32 diff = s - target;
			if (diff <= 0.0f)
			{
				diff = -diff;
			}
			if (diff < rootTol)
			{
				t2 = t;
				break;
			}
			if (s <= target)
			{
				a2 = t;
				sb = s;
			}
			else
			{
				a1 = t;
				sa = s;
			}
			++k;
		} while (k != 50);

		if (t2 < t1 * 1.0000119f)
		{
			return t1;
		}

		t1 = t2;
		++iter;
		if (iter == 1000)
		{
			return t1;
		}

		input->sweepA.GetTransform(&dIn.transformA, t1);
		input->sweepB.GetTransform(&dIn.transformB, t1);
		b2Distance(&dOut, &cache, &dIn);
	}
}

// The bundled (2.1.2-style) solver is kept for reference only.
void b2TimeOfImpactModern(b2TOIOutput* output, const b2TOIInput* input);

void b2TimeOfImpact(b2TOIOutput* output, const b2TOIInput* input)
{
	if (getenv("AB_OLDTOI"))
	{
		b2TimeOfImpactModern(output, input);
		return;
	}
	output->state = b2TOIOutput::e_unknown;
	output->t = b2TimeOfImpactExe(input);
}

void b2TimeOfImpactModern(b2TOIOutput* output, const b2TOIInput* input)
{
	++b2_toiCalls;

	output->state = b2TOIOutput::e_unknown;
	output->t = input->tMax;

	const b2DistanceProxy* proxyA = &input->proxyA;
	const b2DistanceProxy* proxyB = &input->proxyB;

	b2Sweep sweepA = input->sweepA;
	b2Sweep sweepB = input->sweepB;

	// Large rotations can make the root finder fail, so we normalize the
	// sweep angles.
	sweepA.Normalize();
	sweepB.Normalize();

	float32 tMax = input->tMax;

	float32 totalRadius = proxyA->m_radius + proxyB->m_radius;
	float32 target = b2Max(b2_linearSlop, totalRadius - 3.0f * b2_linearSlop);
	float32 tolerance = 0.25f * b2_linearSlop;
	b2Assert(target > tolerance);

	float32 t1 = 0.0f;
	const int32 k_maxIterations = 20;	// TODO_ERIN b2Settings
	int32 iter = 0;

	// Prepare input for distance query.
	b2SimplexCache cache;
	cache.count = 0;
	b2DistanceInput distanceInput;
	distanceInput.proxyA = input->proxyA;
	distanceInput.proxyB = input->proxyB;
	distanceInput.useRadii = false;

	// The outer loop progressively attempts to compute new separating axes.
	// This loop terminates when an axis is repeated (no progress is made).
	for(;;)
	{
		b2Transform xfA, xfB;
		sweepA.GetTransform(&xfA, t1);
		sweepB.GetTransform(&xfB, t1);

		// Get the distance between shapes. We can also use the results
		// to get a separating axis.
		distanceInput.transformA = xfA;
		distanceInput.transformB = xfB;
		b2DistanceOutput distanceOutput;
		b2Distance(&distanceOutput, &cache, &distanceInput);

		// If the shapes are overlapped, we give up on continuous collision.
		if (distanceOutput.distance <= 0.0f)
		{
			// Failure!
			output->state = b2TOIOutput::e_overlapped;
			output->t = 0.0f;
			break;
		}

		if (distanceOutput.distance < target + tolerance)
		{
			// Victory!
			output->state = b2TOIOutput::e_touching;
			output->t = t1;
			break;
		}

		// Initialize the separating axis.
		b2SeparationFunction fcn;
		fcn.Initialize(&cache, proxyA, sweepA, proxyB, sweepB);
#if 0
		// Dump the curve seen by the root finder
		{
			const int32 N = 100;
			float32 dx = 1.0f / N;
			float32 xs[N+1];
			float32 fs[N+1];

			float32 x = 0.0f;

			for (int32 i = 0; i <= N; ++i)
			{
				sweepA.GetTransform(&xfA, x);
				sweepB.GetTransform(&xfB, x);
				float32 f = fcn.Evaluate(xfA, xfB) - target;

				printf("%g %g\n", x, f);

				xs[i] = x;
				fs[i] = f;

				x += dx;
			}
		}
#endif

		// Compute the TOI on the separating axis. We do this by successively
		// resolving the deepest point. This loop is bounded by the number of vertices.
		bool done = false;
		float32 t2 = tMax;
		int32 pushBackIter = 0;
		for (;;)
		{
			// Find the deepest point at t2. Store the witness point indices.
			int32 indexA, indexB;
			float32 s2 = fcn.FindMinSeparation(&indexA, &indexB, t2);

			// Is the final configuration separated?
			if (s2 > target + tolerance)
			{
				// Victory!
				output->state = b2TOIOutput::e_separated;
				output->t = tMax;
				done = true;
				break;
			}

			// Has the separation reached tolerance?
			if (s2 > target - tolerance)
			{
				// Advance the sweeps
				t1 = t2;
				break;
			}

			// Compute the initial separation of the witness points.
			float32 s1 = fcn.Evaluate(indexA, indexB, t1);

			// Check for initial overlap. This might happen if the root finder
			// runs out of iterations.
			if (s1 < target - tolerance)
			{
				output->state = b2TOIOutput::e_failed;
				output->t = t1;
				done = true;
				break;
			}

			// Check for touching
			if (s1 <= target + tolerance)
			{
				// Victory! t1 should hold the TOI (could be 0.0).
				output->state = b2TOIOutput::e_touching;
				output->t = t1;
				done = true;
				break;
			}

			// Compute 1D root of: f(x) - target = 0
			int32 rootIterCount = 0;
			float32 a1 = t1, a2 = t2;
			for (;;)
			{
				// Use a mix of the secant rule and bisection.
				float32 t;
				if (rootIterCount & 1)
				{
					// Secant rule to improve convergence.
					t = a1 + (target - s1) * (a2 - a1) / (s2 - s1);
				}
				else
				{
					// Bisection to guarantee progress.
					t = 0.5f * (a1 + a2);
				}

				float32 s = fcn.Evaluate(indexA, indexB, t);

				if (b2Abs(s - target) < tolerance)
				{
					// t2 holds a tentative value for t1
					t2 = t;
					break;
				}

				// Ensure we continue to bracket the root.
				if (s > target)
				{
					a1 = t;
					s1 = s;
				}
				else
				{
					a2 = t;
					s2 = s;
				}

				++rootIterCount;
				++b2_toiRootIters;

				if (rootIterCount == 50)
				{
					break;
				}
			}

			b2_toiMaxRootIters = b2Max(b2_toiMaxRootIters, rootIterCount);

			++pushBackIter;

			if (pushBackIter == b2_maxPolygonVertices)
			{
				break;
			}
		}

		++iter;
		++b2_toiIters;

		if (done)
		{
			break;
		}

		if (iter == k_maxIterations)
		{
			// Root finder got stuck. Semi-victory.
			output->state = b2TOIOutput::e_failed;
			output->t = t1;
			break;
		}
	}

	b2_toiMaxIters = b2Max(b2_toiMaxIters, iter);
}
