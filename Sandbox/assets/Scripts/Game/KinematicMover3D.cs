using CCEngine;

namespace Game
{
    public sealed class KinematicMover3D : GameScript
    {
        private float _direction = 1.0f;

        protected override void Update(float deltaTime)
        {
            Vector3 position = Translation;
            position.X += _direction * 1.5f * deltaTime;
            if (position.X >= 7.0f)
            {
                position.X = 7.0f;
                _direction = -1.0f;
            }
            else if (position.X <= 3.5f)
            {
                position.X = 3.5f;
                _direction = 1.0f;
            }
            Translation = position;
        }
    }
}
