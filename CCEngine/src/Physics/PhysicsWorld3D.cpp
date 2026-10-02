#include "Physics/PhysicsWorld3D.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace CCEngine
{
    namespace
    {
        using DirectX::XMFLOAT3;

        XMFLOAT3 Add(const XMFLOAT3& a, const XMFLOAT3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
        XMFLOAT3 Sub(const XMFLOAT3& a, const XMFLOAT3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        XMFLOAT3 Mul(const XMFLOAT3& v, float s) { return { v.x * s, v.y * s, v.z * s }; }
        float Dot(const XMFLOAT3& a, const XMFLOAT3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        float LengthSq(const XMFLOAT3& v) { return Dot(v, v); }

        XMFLOAT3 Rotate(const XMFLOAT3& value, const XMFLOAT3& rotation)
        {
            DirectX::XMVECTOR vector = DirectX::XMLoadFloat3(&value);
            vector = DirectX::XMVector3TransformNormal(vector, DirectX::XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z));
            XMFLOAT3 result;
            DirectX::XMStoreFloat3(&result, vector);
            return result;
        }

        XMFLOAT3 BodyCenter(const PhysicsWorld3D::BodyDesc& body)
        {
            XMFLOAT3 scaledOffset = { body.Offset.x * body.Scale.x, body.Offset.y * body.Scale.y, body.Offset.z * body.Scale.z };
            return Add(body.Position, Rotate(scaledOffset, body.Rotation));
        }

        float SphereRadius(const PhysicsWorld3D::BodyDesc& body)
        {
            return body.Size.x * (std::max)({ std::abs(body.Scale.x), std::abs(body.Scale.y), std::abs(body.Scale.z) });
        }

        float InverseMass(const PhysicsWorld3D::BodyDesc& body)
        {
            return body.Type == PhysicsWorld3D::BodyType::Dynamic ? 1.0f / (std::max)(body.Mass, 0.001f) : 0.0f;
        }

        XMFLOAT3 RotatedExtents(const XMFLOAT3& half, const XMFLOAT3& rotation)
        {
            DirectX::XMMATRIX matrix = DirectX::XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);
            DirectX::XMFLOAT4X4 m;
            DirectX::XMStoreFloat4x4(&m, matrix);
            return {
                std::abs(m._11) * half.x + std::abs(m._21) * half.y + std::abs(m._31) * half.z,
                std::abs(m._12) * half.x + std::abs(m._22) * half.y + std::abs(m._32) * half.z,
                std::abs(m._13) * half.x + std::abs(m._23) * half.y + std::abs(m._33) * half.z
            };
        }
    }

    void PhysicsWorld3D::Clear()
    {
        m_Bodies.clear();
        m_ActiveCollisionPairs.clear();
        m_ActiveTriggerPairs.clear();
        m_Events.clear();
    }

    bool PhysicsWorld3D::AddBody(const BodyDesc& desc)
    {
        if (m_Bodies.contains(desc.EntityID))
            return false;

        Body body;
        static_cast<BodyDesc&>(body) = desc;
        body.Mass = (std::max)(body.Mass, 0.001f);
        body.Friction = std::clamp(body.Friction, 0.0f, 1.0f);
        body.Restitution = std::clamp(body.Restitution, 0.0f, 1.0f);
        body.LinearDamping = (std::max)(body.LinearDamping, 0.0f);
        body.AngularDamping = (std::max)(body.AngularDamping, 0.0f);
        m_Bodies.emplace(body.EntityID, body);
        return true;
    }

    void PhysicsWorld3D::RemoveBody(uint32_t entityID)
    {
        m_Bodies.erase(entityID);
        auto removePairs = [entityID](std::unordered_set<uint64_t>& pairs)
        {
            for (auto it = pairs.begin(); it != pairs.end();)
            {
                uint32_t a = static_cast<uint32_t>(*it >> 32);
                uint32_t b = static_cast<uint32_t>(*it);
                it = (a == entityID || b == entityID) ? pairs.erase(it) : std::next(it);
            }
        };
        removePairs(m_ActiveCollisionPairs);
        removePairs(m_ActiveTriggerPairs);
    }

    void PhysicsWorld3D::SetKinematicTransform(uint32_t entityID, const XMFLOAT3& position, const XMFLOAT3& rotation, float deltaTime)
    {
        auto found = m_Bodies.find(entityID);
        if (found == m_Bodies.end() || found->second.Type != BodyType::Kinematic)
            return;

        Body& body = found->second;
        if (deltaTime > 0.0f)
        {
            body.LinearVelocity = Mul(Sub(position, body.Position), 1.0f / deltaTime);
            body.AngularVelocity = Mul(Sub(rotation, body.Rotation), 1.0f / deltaTime);
        }
        body.Position = position;
        body.Rotation = rotation;
    }

    bool PhysicsWorld3D::GetBodyState(uint32_t entityID, BodyState& state) const
    {
        auto found = m_Bodies.find(entityID);
        if (found == m_Bodies.end())
            return false;
        const Body& body = found->second;
        state.Position = body.Position;
        state.Rotation = body.Rotation;
        state.LinearVelocity = body.LinearVelocity;
        state.AngularVelocity = body.AngularVelocity;
        return true;
    }

    uint64_t PhysicsWorld3D::MakePairKey(uint32_t a, uint32_t b)
    {
        if (a > b)
            std::swap(a, b);
        return (static_cast<uint64_t>(a) << 32) | b;
    }

    PhysicsWorld3D::AABB PhysicsWorld3D::ComputeBounds(const Body& body)
    {
        XMFLOAT3 scale = { std::abs(body.Scale.x), std::abs(body.Scale.y), std::abs(body.Scale.z) };
        XMFLOAT3 center = BodyCenter(body);
        XMFLOAT3 half;
        if (body.Shape == ShapeType::Sphere)
        {
            float radius = SphereRadius(body);
            half = { radius, radius, radius };
        }
        else if (body.Shape == ShapeType::Cylinder)
        {
            half = { body.Size.x * scale.x, body.Size.y * scale.y * 0.5f, body.Size.x * scale.z };
            half = RotatedExtents(half, body.Rotation);
        }
        else
        {
            half = { body.Size.x * scale.x * 0.5f, body.Size.y * scale.y * 0.5f, body.Size.z * scale.z * 0.5f };
            half = RotatedExtents(half, body.Rotation);
        }
        return { Sub(center, half), Add(center, half) };
    }

    bool PhysicsWorld3D::TestContact(const Body& a, const Body& b, Contact& contact)
    {
        AABB aa = ComputeBounds(a);
        AABB bb = ComputeBounds(b);

        if (a.Shape == ShapeType::Sphere && b.Shape == ShapeType::Sphere)
        {
            XMFLOAT3 delta = Sub(BodyCenter(b), BodyCenter(a));
            float distanceSq = LengthSq(delta);
            float radiusSum = SphereRadius(a) + SphereRadius(b);
            if (distanceSq >= radiusSum * radiusSum)
                return false;
            float distance = std::sqrt((std::max)(distanceSq, 0.000001f));
            contact.Normal = distanceSq > 0.000001f ? Mul(delta, 1.0f / distance) : XMFLOAT3{ 1.0f, 0.0f, 0.0f };
            contact.Penetration = radiusSum - distance;
            return true;
        }

        auto sphereAgainstBounds = [](const Body& sphere, const AABB& bounds, Contact& result)
        {
            XMFLOAT3 center = BodyCenter(sphere);
            XMFLOAT3 closest = {
                std::clamp(center.x, bounds.Min.x, bounds.Max.x),
                std::clamp(center.y, bounds.Min.y, bounds.Max.y),
                std::clamp(center.z, bounds.Min.z, bounds.Max.z)
            };
            XMFLOAT3 delta = Sub(closest, center);
            float distanceSq = LengthSq(delta);
            float radius = SphereRadius(sphere);
            if (distanceSq >= radius * radius)
                return false;
            if (distanceSq > 0.000001f)
            {
                float distance = std::sqrt(distanceSq);
                result.Normal = Mul(delta, 1.0f / distance);
                result.Penetration = radius - distance;
                return true;
            }
            return false;
        };

        if (a.Shape == ShapeType::Sphere && sphereAgainstBounds(a, bb, contact))
            return true;
        if (b.Shape == ShapeType::Sphere && sphereAgainstBounds(b, aa, contact))
        {
            contact.Normal = Mul(contact.Normal, -1.0f);
            return true;
        }

        float overlapX = (std::min)(aa.Max.x, bb.Max.x) - (std::max)(aa.Min.x, bb.Min.x);
        float overlapY = (std::min)(aa.Max.y, bb.Max.y) - (std::max)(aa.Min.y, bb.Min.y);
        float overlapZ = (std::min)(aa.Max.z, bb.Max.z) - (std::max)(aa.Min.z, bb.Min.z);
        if (overlapX <= 0.0f || overlapY <= 0.0f || overlapZ <= 0.0f)
            return false;

        XMFLOAT3 centerA = Mul(Add(aa.Min, aa.Max), 0.5f);
        XMFLOAT3 centerB = Mul(Add(bb.Min, bb.Max), 0.5f);
        contact.Penetration = overlapX;
        contact.Normal = { centerB.x >= centerA.x ? 1.0f : -1.0f, 0.0f, 0.0f };
        if (overlapY < contact.Penetration)
        {
            contact.Penetration = overlapY;
            contact.Normal = { 0.0f, centerB.y >= centerA.y ? 1.0f : -1.0f, 0.0f };
        }
        if (overlapZ < contact.Penetration)
        {
            contact.Penetration = overlapZ;
            contact.Normal = { 0.0f, 0.0f, centerB.z >= centerA.z ? 1.0f : -1.0f };
        }
        return true;
    }

    void PhysicsWorld3D::ResolveContact(Body& a, Body& b, const Contact& contact)
    {
        float inverseMassA = InverseMass(a);
        float inverseMassB = InverseMass(b);
        float inverseMassSum = inverseMassA + inverseMassB;
        if (inverseMassSum <= 0.0f)
            return;

        // 겹침을 먼저 해소한 뒤 속도 임펄스를 적용해야 낮은 속도에서 물체가 바닥 안으로 서서히 잠기지 않는다.
        constexpr float correctionPercent = 0.8f;
        constexpr float penetrationSlop = 0.001f;
        float correctionMagnitude = (std::max)(contact.Penetration - penetrationSlop, 0.0f) * correctionPercent / inverseMassSum;
        XMFLOAT3 correction = Mul(contact.Normal, correctionMagnitude);
        if (inverseMassA > 0.0f) a.Position = Sub(a.Position, Mul(correction, inverseMassA));
        if (inverseMassB > 0.0f) b.Position = Add(b.Position, Mul(correction, inverseMassB));

        XMFLOAT3 relativeVelocity = Sub(b.LinearVelocity, a.LinearVelocity);
        float normalSpeed = Dot(relativeVelocity, contact.Normal);
        if (normalSpeed > 0.0f)
            return;

        float restitution = (std::max)(a.Restitution, b.Restitution);
        float normalImpulseMagnitude = -(1.0f + restitution) * normalSpeed / inverseMassSum;
        XMFLOAT3 normalImpulse = Mul(contact.Normal, normalImpulseMagnitude);
        if (inverseMassA > 0.0f) a.LinearVelocity = Sub(a.LinearVelocity, Mul(normalImpulse, inverseMassA));
        if (inverseMassB > 0.0f) b.LinearVelocity = Add(b.LinearVelocity, Mul(normalImpulse, inverseMassB));

        relativeVelocity = Sub(b.LinearVelocity, a.LinearVelocity);
        XMFLOAT3 tangent = Sub(relativeVelocity, Mul(contact.Normal, Dot(relativeVelocity, contact.Normal)));
        float tangentLengthSq = LengthSq(tangent);
        if (tangentLengthSq <= 0.000001f)
            return;
        tangent = Mul(tangent, 1.0f / std::sqrt(tangentLengthSq));
        float frictionImpulseMagnitude = -Dot(relativeVelocity, tangent) / inverseMassSum;
        float friction = std::sqrt(a.Friction * b.Friction);
        frictionImpulseMagnitude = std::clamp(frictionImpulseMagnitude, -normalImpulseMagnitude * friction, normalImpulseMagnitude * friction);
        XMFLOAT3 frictionImpulse = Mul(tangent, frictionImpulseMagnitude);
        if (inverseMassA > 0.0f) a.LinearVelocity = Sub(a.LinearVelocity, Mul(frictionImpulse, inverseMassA));
        if (inverseMassB > 0.0f) b.LinearVelocity = Add(b.LinearVelocity, Mul(frictionImpulse, inverseMassB));
    }

    void PhysicsWorld3D::Step(float deltaTime)
    {
        m_Events.clear();
        if (deltaTime <= 0.0f)
            return;

        for (auto& [_, body] : m_Bodies)
        {
            if (body.Type != BodyType::Dynamic)
                continue;
            if (body.UseGravity)
                body.LinearVelocity = Add(body.LinearVelocity, Mul(m_Gravity, deltaTime));
            body.LinearVelocity = Mul(body.LinearVelocity, 1.0f / (1.0f + body.LinearDamping * deltaTime));
            body.Position = Add(body.Position, Mul(body.LinearVelocity, deltaTime));
            if (!body.FixedRotation)
            {
                body.AngularVelocity = Mul(body.AngularVelocity, 1.0f / (1.0f + body.AngularDamping * deltaTime));
                body.Rotation = Add(body.Rotation, Mul(body.AngularVelocity, deltaTime));
            }
        }

        std::unordered_set<uint64_t> currentCollisions;
        std::unordered_set<uint64_t> currentTriggers;
        for (auto first = m_Bodies.begin(); first != m_Bodies.end(); ++first)
        {
            auto second = first;
            for (++second; second != m_Bodies.end(); ++second)
            {
                Body& a = first->second;
                Body& b = second->second;
                if (a.Type == BodyType::Static && b.Type == BodyType::Static)
                    continue;

                Contact contact;
                if (!TestContact(a, b, contact))
                    continue;

                uint64_t pair = MakePairKey(a.EntityID, b.EntityID);
                bool trigger = a.IsTrigger || b.IsTrigger;
                auto& current = trigger ? currentTriggers : currentCollisions;
                auto& active = trigger ? m_ActiveTriggerPairs : m_ActiveCollisionPairs;
                current.insert(pair);
                m_Events.push_back({ a.EntityID, b.EntityID, active.contains(pair) ? EventPhase::Stay : EventPhase::Enter, trigger });
                if (!trigger)
                    ResolveContact(a, b, contact);
            }
        }

        auto appendExits = [this](const std::unordered_set<uint64_t>& active, const std::unordered_set<uint64_t>& current, bool trigger)
        {
            for (uint64_t pair : active)
            {
                if (!current.contains(pair))
                    m_Events.push_back({ static_cast<uint32_t>(pair >> 32), static_cast<uint32_t>(pair), EventPhase::Exit, trigger });
            }
        };
        appendExits(m_ActiveCollisionPairs, currentCollisions, false);
        appendExits(m_ActiveTriggerPairs, currentTriggers, true);
        m_ActiveCollisionPairs = std::move(currentCollisions);
        m_ActiveTriggerPairs = std::move(currentTriggers);
    }
}
