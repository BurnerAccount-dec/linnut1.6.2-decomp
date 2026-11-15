/*
* Copyright (c) 2006-2007 Erin Catto http://www.gphysics.com
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

// Back-ported to Box2D 2.1.1 from the rope joint of Box2D 2.2.0 (altered source version).

#include <Box2D/Dynamics/Joints/b2RopeJoint.h>
#include <Box2D/Dynamics/b2Body.h>
#include <Box2D/Dynamics/b2TimeStep.h>

// Limit:
// C = norm(p2 - p1) - L
// u = (p2 - p1) / norm(p2 - p1)
// Cdot = dot(u, v2 + cross(w2, r2) - v1 - cross(w1, r1))
// J = [-u -cross(r1, u) u cross(r2, u)]
// K = J * invM * JT
//   = invMass1 + invI1 * cross(r1, u)^2 + invMass2 + invI2 * cross(r2, u)^2

b2RopeJoint::b2RopeJoint(const b2RopeJointDef* def)
: b2Joint(def)
{
	m_localAnchor1 = def->localAnchorA;
	m_localAnchor2 = def->localAnchorB;
	m_maxLength = def->maxLength;
	m_mass = 0.0f;
	m_impulse = 0.0f;
	m_state = e_inactiveLimit;
	m_length = 0.0f;
	m_u.SetZero();
}

void b2RopeJoint::InitVelocityConstraints(const b2TimeStep& step)
{
	b2Body* b1 = m_bodyA;
	b2Body* b2 = m_bodyB;

	b2Vec2 r1 = b2Mul(b1->GetTransform().R, m_localAnchor1 - b1->GetLocalCenter());
	b2Vec2 r2 = b2Mul(b2->GetTransform().R, m_localAnchor2 - b2->GetLocalCenter());
	m_u = b2->m_sweep.c + r2 - b1->m_sweep.c - r1;

	m_length = m_u.Length();

	float32 C = m_length - m_maxLength;
	m_state = C > 0.0f ? e_atUpperLimit : e_inactiveLimit;

	if (m_length > b2_linearSlop)
	{
		m_u *= 1.0f / m_length;
	}
	else
	{
		m_u.SetZero();
		m_mass = 0.0f;
		m_impulse = 0.0f;
		return;
	}

	float32 cr1u = b2Cross(r1, m_u);
	float32 cr2u = b2Cross(r2, m_u);
	float32 invMass = b1->m_invMass + b1->m_invI * cr1u * cr1u + b2->m_invMass + b2->m_invI * cr2u * cr2u;
	m_mass = invMass != 0.0f ? 1.0f / invMass : 0.0f;

	if (step.warmStarting)
	{
		// Scale the impulse to support a variable time step.
		m_impulse *= step.dtRatio;

		b2Vec2 P = m_impulse * m_u;
		b1->m_linearVelocity -= b1->m_invMass * P;
		b1->m_angularVelocity -= b1->m_invI * b2Cross(r1, P);
		b2->m_linearVelocity += b2->m_invMass * P;
		b2->m_angularVelocity += b2->m_invI * b2Cross(r2, P);
	}
	else
	{
		m_impulse = 0.0f;
	}
}

void b2RopeJoint::SolveVelocityConstraints(const b2TimeStep& step)
{
	b2Body* b1 = m_bodyA;
	b2Body* b2 = m_bodyB;

	b2Vec2 r1 = b2Mul(b1->GetTransform().R, m_localAnchor1 - b1->GetLocalCenter());
	b2Vec2 r2 = b2Mul(b2->GetTransform().R, m_localAnchor2 - b2->GetLocalCenter());

	// Cdot = dot(u, v + cross(w, r))
	b2Vec2 v1 = b1->m_linearVelocity + b2Cross(b1->m_angularVelocity, r1);
	b2Vec2 v2 = b2->m_linearVelocity + b2Cross(b2->m_angularVelocity, r2);
	float32 C = m_length - m_maxLength;
	float32 Cdot = b2Dot(m_u, v2 - v1);

	// Predictive constraint.
	if (C < 0.0f)
	{
		Cdot += step.inv_dt * C;
	}

	float32 impulse = -m_mass * Cdot;
	float32 oldImpulse = m_impulse;
	m_impulse = b2Min(0.0f, m_impulse + impulse);
	impulse = m_impulse - oldImpulse;

	b2Vec2 P = impulse * m_u;
	b1->m_linearVelocity -= b1->m_invMass * P;
	b1->m_angularVelocity -= b1->m_invI * b2Cross(r1, P);
	b2->m_linearVelocity += b2->m_invMass * P;
	b2->m_angularVelocity += b2->m_invI * b2Cross(r2, P);
}

bool b2RopeJoint::SolvePositionConstraints(float32 baumgarte)
{
	B2_NOT_USED(baumgarte);

	b2Body* b1 = m_bodyA;
	b2Body* b2 = m_bodyB;

	b2Vec2 r1 = b2Mul(b1->GetTransform().R, m_localAnchor1 - b1->GetLocalCenter());
	b2Vec2 r2 = b2Mul(b2->GetTransform().R, m_localAnchor2 - b2->GetLocalCenter());

	b2Vec2 d = b2->m_sweep.c + r2 - b1->m_sweep.c - r1;

	float32 length = d.Normalize();
	float32 C = length - m_maxLength;
	C = b2Clamp(C, 0.0f, b2_maxLinearCorrection);

	float32 impulse = -m_mass * C;
	b2Vec2 P = impulse * d;

	b1->m_sweep.c -= b1->m_invMass * P;
	b1->m_sweep.a -= b1->m_invI * b2Cross(r1, P);
	b2->m_sweep.c += b2->m_invMass * P;
	b2->m_sweep.a += b2->m_invI * b2Cross(r2, P);

	b1->SynchronizeTransform();
	b2->SynchronizeTransform();

	return length - m_maxLength < b2_linearSlop;
}

b2Vec2 b2RopeJoint::GetAnchorA() const
{
	return m_bodyA->GetWorldPoint(m_localAnchor1);
}

b2Vec2 b2RopeJoint::GetAnchorB() const
{
	return m_bodyB->GetWorldPoint(m_localAnchor2);
}

b2Vec2 b2RopeJoint::GetReactionForce(float32 inv_dt) const
{
	b2Vec2 F = (inv_dt * m_impulse) * m_u;
	return F;
}

float32 b2RopeJoint::GetReactionTorque(float32 inv_dt) const
{
	B2_NOT_USED(inv_dt);
	return 0.0f;
}

float32 b2RopeJoint::GetMaxLength() const
{
	return m_maxLength;
}

void b2RopeJoint::SetMaxLength(float32 length)
{
	m_maxLength = length;
}

b2LimitState b2RopeJoint::GetLimitState() const
{
	return m_state;
}
