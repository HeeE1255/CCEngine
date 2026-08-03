using CCEngine;

namespace Game
{
    public sealed class PhysicsEventProbe : GameScript
    {
        protected override void OnCollisionEnter2D(uint otherEntityID)
        {
            Debug.Log($"Collision Enter with entity {otherEntityID}");
            Translation = new Vector3(10.0f, Translation.Y, Translation.Z);
        }

        protected override void OnCollisionStay2D(uint otherEntityID)
        {
            Translation = new Vector3(Translation.X, 20.0f, Translation.Z);
        }

        protected override void OnCollisionExit2D(uint otherEntityID)
        {
            Debug.Warn($"Collision Exit with entity {otherEntityID}");
            Translation = new Vector3(Translation.X, Translation.Y, 30.0f);
        }

        protected override void OnTriggerEnter2D(uint otherEntityID)
        {
            Debug.Log($"Trigger Enter with entity {otherEntityID}");
            Translation = new Vector3(40.0f, Translation.Y, Translation.Z);
        }

        protected override void OnTriggerStay2D(uint otherEntityID)
        {
            Translation = new Vector3(Translation.X, 50.0f, Translation.Z);
        }

        protected override void OnTriggerExit2D(uint otherEntityID)
        {
            Debug.Error($"Trigger Exit with entity {otherEntityID}");
            Translation = new Vector3(Translation.X, Translation.Y, 60.0f);
        }
    }
}
