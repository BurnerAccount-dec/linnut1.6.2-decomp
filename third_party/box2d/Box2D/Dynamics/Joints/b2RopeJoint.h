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

#ifndef B2_ROPE_JOINT_H
#define B2_ROPE_JOINT_H

#include <Box2D/Dynamics/Joints/b2Joint.h>

/// Rope joint definition. The anchors are local points; the joint keeps them
/// no further apart than maxLength (they may come closer).
struct b2RopeJointDef : public b2JointDef
{
	b2RopeJointDef()
	{
		type = e_ropeJoint;
		localAnchorA.Set(-1.0f, 0.0f);
		localAnchorB.Set(1.0f, 0.0f);
		maxLength = 0.0f;
	}

	/// The local anchor point relative to bodyA's origin.
	b2Vec2 localAnchorA;

	/// The local anchor point relative to bodyB's origin.
	b2Vec2 localAnchorB;

	/// The maximum length of the rope.
	/// Warning: this must be larger than b2_linearSlop or the joint will have no effect.
	float32 maxLength;
};

/// A rope joint enforces a maximum distance between two points on two bodies.
/// It has no other effect.
class b2RopeJoint : public b2Joint
{
public:
	b2Vec2 GetAnchorA() const;
	b2Vec2 GetAnchorB() const;

	b2Vec2 GetReactionForce(float32 inv_dt) const;
	float32 GetReactionTorque(float32 inv_dt) const;

	/// Get the maximum length of the rope.
	float32 GetMaxLength() const;
	/// Set the maximum length of the rope.
	void SetMaxLength(float32 length);

	b2LimitState GetLimitState() const;

protected:
	friend class b2Joint;
	b2RopeJoint(const b2RopeJointDef* data);

	void InitVelocityConstraints(const b2TimeStep& step);
	void SolveVelocityConstraints(const b2TimeStep& step);
	bool SolvePositionConstraints(float32 baumgarte);

	b2Vec2 m_localAnchor1;
	b2Vec2 m_localAnchor2;

	float32 m_maxLength;
	float32 m_length;

	float32 m_impulse;
	float32 m_mass;
	b2Vec2 m_u;
	b2LimitState m_state;
};

#endif
