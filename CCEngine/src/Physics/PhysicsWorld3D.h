#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CCEngine
{
    class PhysicsWorld3D
    {
    public:
        enum class BodyType : uint8_t { Static = 0, Dynamic, Kinematic };
        enum class ShapeType : uint8_t { Box = 0, Sphere, Cylinder, MeshBounds };
        enum class EventPhase : uint8_t { Enter = 0, Stay, Exit };

        struct BodyDesc
        {
            uint32_t EntityID = 0;
            BodyType Type = BodyType::Static;
            ShapeType Shape = ShapeType::Box;
            DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 Rotation = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT3 Offset = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 Size = { 1.0f, 1.0f, 1.0f };
            DirectX::XMFLOAT3 LinearVelocity = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 AngularVelocity = { 0.0f, 0.0f, 0.0f };
            float Mass = 1.0f;
            float LinearDamping = 0.05f;
            float AngularDamping = 0.05f;
            float Friction = 0.5f;
            float Restitution = 0.0f;
            bool UseGravity = true;
            bool FixedRotation = false;
            bool IsTrigger = false;
        };

        struct BodyState
        {
            DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 Rotation = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 LinearVelocity = { 0.0f, 0.0f, 0.0f };
            DirectX::XMFLOAT3 AngularVelocity = { 0.0f, 0.0f, 0.0f };
        };

        struct Event
        {
            uint32_t EntityA = 0;
            uint32_t EntityB = 0;
            EventPhase Phase = EventPhase::Enter;
            bool IsTrigger = false;
        };

        void Clear();
        bool AddBody(const BodyDesc& desc);
        void RemoveBody(uint32_t entityID);
        void SetKinematicTransform(uint32_t entityID, const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& rotation, float deltaTime);
        bool GetBodyState(uint32_t entityID, BodyState& state) const;
        void Step(float deltaTime);
        const std::vector<Event>& GetEvents() const { return m_Events; }

        void SetGravity(const DirectX::XMFLOAT3& gravity) { m_Gravity = gravity; }
        const DirectX::XMFLOAT3& GetGravity() const { return m_Gravity; }

    private:
        struct Body : BodyDesc {};
        struct AABB
        {
            DirectX::XMFLOAT3 Min;
            DirectX::XMFLOAT3 Max;
        };
        struct Contact
        {
            DirectX::XMFLOAT3 Normal = { 0.0f, 1.0f, 0.0f };
            float Penetration = 0.0f;
        };

        static uint64_t MakePairKey(uint32_t a, uint32_t b);
        static AABB ComputeBounds(const Body& body);
        static bool TestContact(const Body& a, const Body& b, Contact& contact);
        static void ResolveContact(Body& a, Body& b, const Contact& contact);

        DirectX::XMFLOAT3 m_Gravity = { 0.0f, -9.81f, 0.0f };
        std::unordered_map<uint32_t, Body> m_Bodies;
        std::unordered_set<uint64_t> m_ActiveCollisionPairs;
        std::unordered_set<uint64_t> m_ActiveTriggerPairs;
        std::vector<Event> m_Events;
    };
}
