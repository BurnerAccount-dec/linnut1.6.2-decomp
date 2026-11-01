/*
* Copyright (c) 2006-2011 Erin Catto http://www.box2d.org
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

#include <Box2D/Dynamics/Joints/b2RevoluteJoint.h>
#include <Box2D/Dynamics/b2Body.h>
#include <Box2D/Dynamics/b2TimeStep.h>

// Point-to-point constraint
// C = p2 - p1
// Cdot = v2 - v1
//      = v2 + cross(w2, r2) - v1 - cross(w1, r1)
// J = [-I -r1_skew I r2_skew ]
// Identity used:
// w k % (rx i + ry j) = w * (-ry i + rx j)

// Motor constraint
// Cdot = w2 - w1
// J = [0 0 -1 0 0 1]
// K = invI1 + invI2

void b2RevoluteJointDef::Initialize(b2Body* bA, b2Body* bB, const b2Vec2& anchor)
{
	bodyA = bA;
	bodyB = bB;
	localAnchorA = bodyA->GetLocalPoint(anchor);
	localAnchorB = bodyB->GetLocalPoint(anchor);
	referenceAngle = bodyB->GetAngle() - bodyA->GetAngle();
}

b2RevoluteJoint::b2RevoluteJoint(const b2RevoluteJointDef* def)
: b2Joint(def)
{
	m_localAnchorA = def->localAnchorA;
	m_localAnchorB = def->localAnchorB;
	m_referenceAngle = def->referenceAngle;

	m_impulse.SetZero();
	m_motorImpulse = 0.0f;

	m_lowerAngle = def->lowerAngle;
	m_upperAngle = def->upperAngle;
	m_maxMotorTorque = def->maxMotorTorque;
	m_motorSpeed = def->motorSpeed;
	m_enableLimit = def->enableLimit;
	m_enableMotor = def->enableMotor;
	m_limitState = e_inactiveLimit;
}

// AngryBirdsRio.exe solves its joints the Box2D 2.1 way (revolute: 0x4e1150 init, 0x4e17b0 velocity,
// 0x4e1cf0 position): on the bodies' sweep and velocity, with the old impulse accumulation and the
// "large detachment" particle step, instead of the 2.2.0 solver. The island hands the joints its position
// and velocity arrays, which at these points hold exactly what the exe reads from the bodies, so the 2.1
// code below works on local copies of that state and writes it back.
void b2RevoluteJoint::InitVelocityConstraints(const b2SolverData& data)
{
	m_indexA = m_bodyA->m_islandIndex;
	m_indexB = m_bodyB->m_islandIndex;
	m_localCenterA = m_bodyA->m_sweep.localCenter;
	m_localCenterB = m_bodyB->m_sweep.localCenter;
	m_invMassA = m_bodyA->m_invMass;
	m_invMassB = m_bodyB->m_invMass;
	m_invIA = m_bodyA->m_invI;
	m_invIB = m_bodyB->m_invI;

	float32 a1 = data.positions[m_indexA].a;
	float32 a2 = data.positions[m_indexB].a;
	b2Vec2 v1 = data.velocities[m_indexA].v;
	float32 w1 = data.velocities[m_indexA].w;
	b2Vec2 v2 = data.velocities[m_indexB].v;
	float32 w2 = data.velocities[m_indexB].w;

	b2Rot q1(a1), q2(a2);

	// Compute the effective mass matrix.
	b2Vec2 r1 = b2Mul(q1, m_localAnchorA - m_localCenterA);
	b2Vec2 r2 = b2Mul(q2, m_localAnchorB - m_localCenterB);

	float32 m1 = m_invMassA, m2 = m_invMassB;
	float32 i1 = m_invIA, i2 = m_invIB;

	m_mass.ex.x = m1 + m2 + r1.y * r1.y * i1 + r2.y * r2.y * i2;
	m_mass.ey.x = -r1.y * r1.x * i1 - r2.y * r2.x * i2;
	m_mass.ez.x = -r1.y * i1 - r2.y * i2;
	m_mass.ex.y = m_mass.ey.x;
	m_mass.ey.y = m1 + m2 + r1.x * r1.x * i1 + r2.x * r2.x * i2;
	m_mass.ez.y = r1.x * i1 + r2.x * i2;
	m_mass.ex.z = m_mass.ez.x;
	m_mass.ey.z = m_mass.ez.y;
	m_mass.ez.z = i1 + i2;

	m_motorMass = i1 + i2;
	if (m_motorMass > 0.0f)
	{
		m_motorMass = 1.0f / m_motorMass;
	}

	if (m_enableMotor == false)
	{
		m_motorImpulse = 0.0f;
	}

	if (m_enableLimit)
	{
		float32 jointAngle = a2 - a1 - m_referenceAngle;
		if (b2Abs(m_upperAngle - m_lowerAngle) < 2.0f * b2_angularSlop)
		{
			m_limitState = e_equalLimits;
		}
		else if (jointAngle <= m_lowerAngle)
		{
			if (m_limitState != e_atLowerLimit)
			{
				m_impulse.z = 0.0f;
			}
			m_limitState = e_atLowerLimit;
		}
		else if (jointAngle > m_upperAngle)	// the exe tests "greater", not "greater or equal" (0x4e1150)
		{
			if (m_limitState != e_atUpperLimit)
			{
				m_impulse.z = 0.0f;
			}
			m_limitState = e_atUpperLimit;
		}
		else
		{
			m_limitState = e_inactiveLimit;
			m_impulse.z = 0.0f;
		}
	}
	else
	{
		m_limitState = e_inactiveLimit;
	}

	if (data.step.warmStarting)
	{
		// Scale impulses to support a variable time step.
		m_impulse *= data.step.dtRatio;
		m_motorImpulse *= data.step.dtRatio;

		b2Vec2 P(m_impulse.x, m_impulse.y);

		v1 -= m1 * P;
		w1 -= i1 * (b2Cross(r1, P) + m_motorImpulse + m_impulse.z);

		v2 += m2 * P;
		w2 += i2 * (b2Cross(r2, P) + m_motorImpulse + m_impulse.z);
	}
	else
	{
		m_impulse.SetZero();
		m_motorImpulse = 0.0f;
	}

	data.velocities[m_indexA].v = v1;
	data.velocities[m_indexA].w = w1;
	data.velocities[m_indexB].v = v2;
	data.velocities[m_indexB].w = w2;
}

void b2RevoluteJoint::SolveVelocityConstraints(const b2SolverData& data)
{
	b2Vec2 v1 = data.velocities[m_indexA].v;
	float32 w1 = data.velocities[m_indexA].w;
	b2Vec2 v2 = data.velocities[m_indexB].v;
	float32 w2 = data.velocities[m_indexB].w;

	b2Rot q1(data.positions[m_indexA].a), q2(data.positions[m_indexB].a);

	float32 m1 = m_invMassA, m2 = m_invMassB;
	float32 i1 = m_invIA, i2 = m_invIB;

	// Solve motor constraint.
	if (m_enableMotor && m_limitState != e_equalLimits)
	{
		float32 Cdot = w2 - w1 - m_motorSpeed;
		float32 impulse = m_motorMass * (-Cdot);
		float32 oldImpulse = m_motorImpulse;
		float32 maxImpulse = data.step.dt * m_maxMotorTorque;
		m_motorImpulse = b2Clamp(m_motorImpulse + impulse, -maxImpulse, maxImpulse);
		impulse = m_motorImpulse - oldImpulse;

		w1 -= i1 * impulse;
		w2 += i2 * impulse;
	}

	// Solve limit constraint.
	if (m_enableLimit && m_limitState != e_inactiveLimit)
	{
		b2Vec2 r1 = b2Mul(q1, m_localAnchorA - m_localCenterA);
		b2Vec2 r2 = b2Mul(q2, m_localAnchorB - m_localCenterB);

		// Solve point-to-point constraint
		b2Vec2 Cdot1 = v2 + b2Cross(w2, r2) - v1 - b2Cross(w1, r1);
		float32 Cdot2 = w2 - w1;
		b2Vec3 Cdot(Cdot1.x, Cdot1.y, Cdot2);

		b2Vec3 impulse = m_mass.Solve33(-Cdot);

		if (m_limitState == e_equalLimits)
		{
			m_impulse += impulse;
		}
		else if (m_limitState == e_atLowerLimit)
		{
			float32 newImpulse = m_impulse.z + impulse.z;
			if (newImpulse < 0.0f)
			{
				b2Vec2 reduced = m_mass.Solve22(-Cdot1);
				impulse.x = reduced.x;
				impulse.y = reduced.y;
				impulse.z = -m_impulse.z;
				m_impulse.x += reduced.x;
				m_impulse.y += reduced.y;
				m_impulse.z = 0.0f;
			}
		}
		else if (m_limitState == e_atUpperLimit)
		{
			float32 newImpulse = m_impulse.z + impulse.z;
			if (newImpulse > 0.0f)
			{
				b2Vec2 reduced = m_mass.Solve22(-Cdot1);
				impulse.x = reduced.x;
				impulse.y = reduced.y;
				impulse.z = -m_impulse.z;
				m_impulse.x += reduced.x;
				m_impulse.y += reduced.y;
				m_impulse.z = 0.0f;
			}
		}

		b2Vec2 P(impulse.x, impulse.y);

		v1 -= m1 * P;
		w1 -= i1 * (b2Cross(r1, P) + impulse.z);

		v2 += m2 * P;
		w2 += i2 * (b2Cross(r2, P) + impulse.z);
	}
	else
	{
		b2Vec2 r1 = b2Mul(q1, m_localAnchorA - m_localCenterA);
		b2Vec2 r2 = b2Mul(q2, m_localAnchorB - m_localCenterB);

		// Solve point-to-point constraint
		b2Vec2 Cdot = v2 + b2Cross(w2, r2) - v1 - b2Cross(w1, r1);
		b2Vec2 impulse = m_mass.Solve22(-Cdot);

		m_impulse.x += impulse.x;
		m_impulse.y += impulse.y;

		v1 -= m1 * impulse;
		w1 -= i1 * b2Cross(r1, impulse);

		v2 += m2 * impulse;
		w2 += i2 * b2Cross(r2, impulse);
	}

	data.velocities[m_indexA].v = v1;
	data.velocities[m_indexA].w = w1;
	data.velocities[m_indexB].v = v2;
	data.velocities[m_indexB].w = w2;
}

bool b2RevoluteJoint::SolvePositionConstraints(const b2SolverData& data)
{
	b2Vec2 c1 = data.positions[m_indexA].c;
	float32 a1 = data.positions[m_indexA].a;
	b2Vec2 c2 = data.positions[m_indexB].c;
	float32 a2 = data.positions[m_indexB].a;

	float32 angularError = 0.0f;
	float32 positionError = 0.0f;

	// Solve angular limit constraint.
	if (m_enableLimit && m_limitState != e_inactiveLimit)
	{
		float32 angle = a2 - a1 - m_referenceAngle;
		float32 limitImpulse = 0.0f;

		if (m_limitState == e_equalLimits)
		{
			// Prevent large angular corrections
			float32 C = b2Clamp(angle - m_lowerAngle, -b2_maxAngularCorrection, b2_maxAngularCorrection);
			limitImpulse = -m_motorMass * C;
			angularError = b2Abs(C);
		}
		else if (m_limitState == e_atLowerLimit)
		{
			float32 C = angle - m_lowerAngle;
			angularError = -C;

			// Prevent large angular corrections and allow some slop.
			C = b2Clamp(C + b2_angularSlop, -b2_maxAngularCorrection, 0.0f);
			limitImpulse = -m_motorMass * C;
		}
		else if (m_limitState == e_atUpperLimit)
		{
			float32 C = angle - m_upperAngle;
			angularError = C;

			// Prevent large angular corrections and allow some slop.
			C = b2Clamp(C - b2_angularSlop, 0.0f, b2_maxAngularCorrection);
			limitImpulse = -m_motorMass * C;
		}

		a1 -= m_invIA * limitImpulse;
		a2 += m_invIB * limitImpulse;
	}

	// Solve point-to-point constraint.
	{
		b2Rot q1(a1), q2(a2);
		b2Vec2 r1 = b2Mul(q1, m_localAnchorA - m_localCenterA);
		b2Vec2 r2 = b2Mul(q2, m_localAnchorB - m_localCenterB);

		b2Vec2 C = c2 + r2 - c1 - r1;
		positionError = C.Length();

		float32 invMass1 = m_invMassA, invMass2 = m_invMassB;
		float32 invI1 = m_invIA, invI2 = m_invIB;

		// Handle large detachment.
		const float32 k_allowedStretch = 10.0f * b2_linearSlop;
		if (C.LengthSquared() > k_allowedStretch * k_allowedStretch)
		{
			// Use a particle solution (no rotation).
			b2Vec2 u = C; u.Normalize();
			float32 m = invMass1 + invMass2;
			if (m > 0.0f)
			{
				m = 1.0f / m;
			}
			b2Vec2 impulse = m * (-C);
			const float32 k_beta = 0.5f;
			c1 -= k_beta * invMass1 * impulse;
			c2 += k_beta * invMass2 * impulse;

			C = c2 + r2 - c1 - r1;
		}

		b2Mat22 K1;
		K1.ex.x = invMass1 + invMass2;	K1.ey.x = 0.0f;
		K1.ex.y = 0.0f;					K1.ey.y = invMass1 + invMass2;

		b2Mat22 K2;
		K2.ex.x =  invI1 * r1.y * r1.y;	K2.ey.x = -invI1 * r1.x * r1.y;
		K2.ex.y = -invI1 * r1.x * r1.y;	K2.ey.y =  invI1 * r1.x * r1.x;

		b2Mat22 K3;
		K3.ex.x =  invI2 * r2.y * r2.y;	K3.ey.x = -invI2 * r2.x * r2.y;
		K3.ex.y = -invI2 * r2.x * r2.y;	K3.ey.y =  invI2 * r2.x * r2.x;

		b2Mat22 K = K1 + K2 + K3;
		b2Vec2 impulse = K.Solve(-C);

		c1 -= invMass1 * impulse;
		a1 -= invI1 * b2Cross(r1, impulse);

		c2 += invMass2 * impulse;
		a2 += invI2 * b2Cross(r2, impulse);
	}

	data.positions[m_indexA].c = c1;
	data.positions[m_indexA].a = a1;
	data.positions[m_indexB].c = c2;
	data.positions[m_indexB].a = a2;

	return positionError <= b2_linearSlop && angularError <= b2_angularSlop;
}

b2Vec2 b2RevoluteJoint::GetAnchorA() const
{
	return m_bodyA->GetWorldPoint(m_localAnchorA);
}

b2Vec2 b2RevoluteJoint::GetAnchorB() const
{
	return m_bodyB->GetWorldPoint(m_localAnchorB);
}

b2Vec2 b2RevoluteJoint::GetReactionForce(float32 inv_dt) const
{
	b2Vec2 P(m_impulse.x, m_impulse.y);
	return inv_dt * P;
}

float32 b2RevoluteJoint::GetReactionTorque(float32 inv_dt) const
{
	return inv_dt * m_impulse.z;
}

float32 b2RevoluteJoint::GetJointAngle() const
{
	b2Body* bA = m_bodyA;
	b2Body* bB = m_bodyB;
	return bB->m_sweep.a - bA->m_sweep.a - m_referenceAngle;
}

float32 b2RevoluteJoint::GetJointSpeed() const
{
	b2Body* bA = m_bodyA;
	b2Body* bB = m_bodyB;
	return bB->m_angularVelocity - bA->m_angularVelocity;
}

bool b2RevoluteJoint::IsMotorEnabled() const
{
	return m_enableMotor;
}

void b2RevoluteJoint::EnableMotor(bool flag)
{
	m_bodyA->SetAwake(true);
	m_bodyB->SetAwake(true);
	m_enableMotor = flag;
}

float32 b2RevoluteJoint::GetMotorTorque(float32 inv_dt) const
{
	return inv_dt * m_motorImpulse;
}

void b2RevoluteJoint::SetMotorSpeed(float32 speed)
{
	m_bodyA->SetAwake(true);
	m_bodyB->SetAwake(true);
	m_motorSpeed = speed;
}

void b2RevoluteJoint::SetMaxMotorTorque(float32 torque)
{
	m_bodyA->SetAwake(true);
	m_bodyB->SetAwake(true);
	m_maxMotorTorque = torque;
}

bool b2RevoluteJoint::IsLimitEnabled() const
{
	return m_enableLimit;
}

void b2RevoluteJoint::EnableLimit(bool flag)
{
	if (flag != m_enableLimit)
	{
		m_bodyA->SetAwake(true);
		m_bodyB->SetAwake(true);
		m_enableLimit = flag;
		m_impulse.z = 0.0f;
	}
}

float32 b2RevoluteJoint::GetLowerLimit() const
{
	return m_lowerAngle;
}

float32 b2RevoluteJoint::GetUpperLimit() const
{
	return m_upperAngle;
}

void b2RevoluteJoint::SetLimits(float32 lower, float32 upper)
{
	b2Assert(lower <= upper);
	
	if (lower != m_lowerAngle || upper != m_upperAngle)
	{
		m_bodyA->SetAwake(true);
		m_bodyB->SetAwake(true);
		m_impulse.z = 0.0f;
		m_lowerAngle = lower;
		m_upperAngle = upper;
	}
}
