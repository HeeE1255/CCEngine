using CCEngine;

namespace Game
{
    public sealed class CubeMove : GameScript
    {
        //public float Speed = 1.0f;              // 기본: 직접 입력

        [Range(0.0f, 10.0f)]
        public float Speed = 1.0f;              // 바/슬라이더

        //[Drag(0.1f)]
        //public float Speed = 1.0f;              // 드래그 조절

        [Step(1)]
        public int Health = 100;                // - / + 버튼

        [ReadOnly]
        public float RuntimeValue = 0.0f;       // 표시만 가능

        protected override void OnCreate()
        {
            // Play가 시작되어 이 스크립트가 생성될 때 한 번 호출됩니다.
        }

        protected override void OnUpdate(float deltaTime)
        {
            // Play 중 매 프레임 호출되며 deltaTime은 이전 프레임부터 흐른 시간입니다.
            Translation += new Vector3(0.0f, Speed * deltaTime, 0.0f);

        }

        protected override void OnDestroy()
        {
            // Play가 끝나거나 스크립트 인스턴스가 제거될 때 한 번 호출됩니다.
        }
    }
}
