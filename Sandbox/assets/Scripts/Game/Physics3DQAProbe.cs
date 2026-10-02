using CCEngine;

namespace Game
{
    public sealed class Physics3DQAProbe : GameScript
    {
        private bool _collisionStayLogged;
        private bool _triggerStayLogged;

        protected override void OnCollisionEnter3D(uint otherEntityID)
        {
            Debug.Log($"[Physics3D QA] Collision Enter with entity {otherEntityID}");
        }

        protected override void OnCollisionStay3D(uint otherEntityID)
        {
            if (_collisionStayLogged)
                return;
            _collisionStayLogged = true;
            Debug.Log($"[Physics3D QA] Collision Stay with entity {otherEntityID}");
        }

        protected override void OnCollisionExit3D(uint otherEntityID)
        {
            Debug.Log($"[Physics3D QA] Collision Exit with entity {otherEntityID}");
        }

        protected override void OnTriggerEnter3D(uint otherEntityID)
        {
            Debug.Log($"[Physics3D QA] Trigger Enter with entity {otherEntityID}");
        }

        protected override void OnTriggerStay3D(uint otherEntityID)
        {
            if (_triggerStayLogged)
                return;
            _triggerStayLogged = true;
            Debug.Log($"[Physics3D QA] Trigger Stay with entity {otherEntityID}");
        }

        protected override void OnTriggerExit3D(uint otherEntityID)
        {
            Debug.Log($"[Physics3D QA] Trigger Exit with entity {otherEntityID}");
        }
    }
}
