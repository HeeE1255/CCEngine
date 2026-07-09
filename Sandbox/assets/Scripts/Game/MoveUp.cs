using CCEngine;

namespace Game
{
    public sealed class MoveUp : GameScript
    {
        public float Speed = 1.0f;

        protected override void OnCreate()
        {
            Log("MoveUp started.");
        }

        protected override void OnUpdate(float deltaTime)
        {
            Translation += new Vector3(0.0f, Speed * deltaTime, 0.0f);
        }
    }
}
