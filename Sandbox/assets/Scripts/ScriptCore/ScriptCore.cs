#nullable enable
using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text.Json;

namespace CCEngine
{
    [AttributeUsage(AttributeTargets.Field)]
    public sealed class RangeAttribute : Attribute
    {
        public float Min { get; }
        public float Max { get; }

        public RangeAttribute(float min, float max)
        {
            Min = min;
            Max = max;
        }
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class DragAttribute : Attribute
    {
        public float Speed { get; }
        public DragAttribute(float speed = 0.1f) => Speed = speed;
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class StepAttribute : Attribute
    {
        public float Value { get; }
        public StepAttribute(float value = 1.0f) => Value = value;
    }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class ReadOnlyAttribute : Attribute { }

    [StructLayout(LayoutKind.Sequential)]
    public struct Vector3
    {
        public float X;
        public float Y;
        public float Z;

        public Vector3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public static Vector3 operator +(Vector3 left, Vector3 right)
            => new(left.X + right.X, left.Y + right.Y, left.Z + right.Z);

        public static Vector3 operator *(Vector3 value, float scalar)
            => new(value.X * scalar, value.Y * scalar, value.Z * scalar);
    }

    public abstract class GameScript
    {
        internal uint EntityID { get; set; }

        public Vector3 Translation
        {
            get => Internal.NativeApi.GetTranslation(EntityID);
            set => Internal.NativeApi.SetTranslation(EntityID, value);
        }

        public void Log(string message) => Debug.Log(message);

        protected virtual void Awake() => OnCreate();
        protected virtual void OnEnable() { }
        protected virtual void Start() { }
        protected virtual void FixedUpdate(float deltaTime) { }
        protected virtual void Update(float deltaTime) => OnUpdate(deltaTime);
        protected virtual void LateUpdate(float deltaTime) { }
        protected virtual void OnDisable() { }
        protected virtual void OnCreate() { }
        protected virtual void OnUpdate(float deltaTime) { }
        protected virtual void OnDestroy() { }
        protected virtual void OnCollisionEnter2D(uint otherEntityID) { }
        protected virtual void OnCollisionStay2D(uint otherEntityID) { }
        protected virtual void OnCollisionExit2D(uint otherEntityID) { }
        protected virtual void OnTriggerEnter2D(uint otherEntityID) { }
        protected virtual void OnTriggerStay2D(uint otherEntityID) { }
        protected virtual void OnTriggerExit2D(uint otherEntityID) { }

        internal void InvokeAwake() => Awake();
        internal void InvokeOnEnable() => OnEnable();
        internal void InvokeStart() => Start();
        internal void InvokeFixedUpdate(float deltaTime) => FixedUpdate(deltaTime);
        internal void InvokeUpdate(float deltaTime) => Update(deltaTime);
        internal void InvokeLateUpdate(float deltaTime) => LateUpdate(deltaTime);
        internal void InvokeOnDisable() => OnDisable();
        internal void InvokeDestroy() => OnDestroy();
        internal void InvokePhysicsEvent(int eventType, uint otherEntityID)
        {
            // 네이티브 물리 큐가 전달한 이벤트 번호를 사용자 스크립트의 가상 메서드로 바꿔 호출한다.
            // 이렇게 두면 엔진 쪽 이벤트 순서와 C# 작성 방식이 서로 느슨하게 분리된다.
            switch (eventType)
            {
                case 0: OnCollisionEnter2D(otherEntityID); break;
                case 1: OnCollisionStay2D(otherEntityID); break;
                case 2: OnCollisionExit2D(otherEntityID); break;
                case 3: OnTriggerEnter2D(otherEntityID); break;
                case 4: OnTriggerStay2D(otherEntityID); break;
                case 5: OnTriggerExit2D(otherEntityID); break;
            }
        }
    }

    // 사용자 스크립트가 에디터 Console에 직접 메시지를 남길 때 쓰는 진입점이다.
    // 엔진 내부 로그와 구분하기 위해 네이티브 쪽에서 [C#] 접두어를 붙인다.
    public static class Debug
    {
        public static void Log(string message) => Internal.NativeApi.Log(message, Internal.NativeLogLevel.Info);
        public static void Warn(string message) => Internal.NativeApi.Log(message, Internal.NativeLogLevel.Warning);
        public static void Error(string message) => Internal.NativeApi.Log(message, Internal.NativeLogLevel.Error);
    }
}

namespace CCEngine.Internal
{
    internal enum NativeLogLevel
    {
        Info = 0,
        Warning = 1,
        Error = 2
    }

    internal static unsafe class NativeApi
    {
        private static delegate* unmanaged[Cdecl]<uint, float*, float*, float*, int> s_GetTranslation;
        private static delegate* unmanaged[Cdecl]<uint, float, float, float, void> s_SetTranslation;
        private static delegate* unmanaged[Cdecl]<byte*, int, void> s_Log;

        internal static void Bind(nint getTranslation, nint setTranslation, nint log)
        {
            s_GetTranslation = (delegate* unmanaged[Cdecl]<uint, float*, float*, float*, int>)getTranslation;
            s_SetTranslation = (delegate* unmanaged[Cdecl]<uint, float, float, float, void>)setTranslation;
            s_Log = (delegate* unmanaged[Cdecl]<byte*, int, void>)log;
        }

        internal static CCEngine.Vector3 GetTranslation(uint entityID)
        {
            float x = 0, y = 0, z = 0;
            s_GetTranslation(entityID, &x, &y, &z);
            return new CCEngine.Vector3(x, y, z);
        }

        internal static void SetTranslation(uint entityID, CCEngine.Vector3 value)
            => s_SetTranslation(entityID, value.X, value.Y, value.Z);

        internal static void Log(string message, NativeLogLevel level = NativeLogLevel.Info)
        {
            // C# 문자열은 관리 메모리에 있으므로 네이티브로 넘기기 전에 UTF-8 바이트로 고정한다.
            // 로그 레벨을 같이 넘겨야 Console 패널에서 Info, Warning, Error 색을 다르게 표시할 수 있다.
            byte[] utf8 = System.Text.Encoding.UTF8.GetBytes(message + '\0');
            fixed (byte* text = utf8)
                s_Log(text, (int)level);
        }
    }

    internal sealed class GameAssemblyLoadContext : AssemblyLoadContext
    {
        internal GameAssemblyLoadContext() : base("CCEngine.GameScripts", isCollectible: true) { }

        protected override Assembly? Load(AssemblyName assemblyName)
        {
            // 게임 어셈블리가 참조하는 ScriptCore는 현재 호스트가 실행 중인 어셈블리와 같은 인스턴스를 돌려준다.
            // 별도로 다시 로드하면 GameScript 이름이 같아도 CLR에서는 서로 다른 형식으로 판단한다.
            if (assemblyName.Name == "CCEngine.ScriptCore")
                return typeof(CCEngine.GameScript).Assembly;
            return null;
        }
    }

    internal static unsafe class ScriptHost
    {
        private sealed class ScriptInstanceState
        {
            internal CCEngine.GameScript Instance;

            internal ScriptInstanceState(CCEngine.GameScript instance)
            {
                Instance = instance;
            }
        }

        private static readonly Dictionary<uint, ScriptInstanceState> s_Instances = new();
        private static GameAssemblyLoadContext? s_LoadContext;
        private static Assembly? s_GameAssembly;

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int Initialize(nint getTranslation, nint setTranslation, nint log, nint assemblyPath)
        {
            try
            {
                NativeApi.Bind(getTranslation, setTranslation, log);
                string? path = Marshal.PtrToStringUni(assemblyPath);
                if (string.IsNullOrWhiteSpace(path))
                    return -1;

                s_LoadContext = new GameAssemblyLoadContext();
                s_GameAssembly = s_LoadContext.LoadFromAssemblyPath(path);
                return 0;
            }
            catch (Exception exception)
            {
                NativeApi.Log(exception.ToString());
                return -1;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static int CreateInstance(uint entityID, nint className, nint fieldJson)
        {
            try
            {
                string? name = Marshal.PtrToStringUTF8(className);
                string? overridesJson = Marshal.PtrToStringUTF8(fieldJson);
                Type? type = s_GameAssembly?.GetType(name ?? string.Empty, throwOnError: false);
                if (type == null || type.IsAbstract || !typeof(CCEngine.GameScript).IsAssignableFrom(type))
                {
                    NativeApi.Log($"Script class not found: {name}");
                    if (s_GameAssembly != null)
                    {
                        foreach (Type availableType in s_GameAssembly.GetTypes())
                            NativeApi.Log($"Available script type: {availableType.FullName}");
                    }
                    return -1;
                }

                if (Activator.CreateInstance(type) is not CCEngine.GameScript instance)
                    return -1;

                instance.EntityID = entityID;
                s_Instances[entityID] = new ScriptInstanceState(instance);
                ApplyFieldOverrides(instance, overridesJson);
                return 0;
            }
            catch (Exception exception)
            {
                NativeApi.Log(exception.ToString());
                return -1;
            }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void DestroyInstance(uint entityID)
        {
            // OnDestroy 호출 순서는 네이티브 Scene이 관리한다.
            // 여기서는 관리 객체를 테이블에서 제거만 해야 같은 이벤트가 두 번 호출되지 않는다.
            s_Instances.Remove(entityID);
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void InvokeLifecycleInstance(uint entityID, int eventType, float deltaTime)
        {
            if (!s_Instances.TryGetValue(entityID, out ScriptInstanceState? state))
                return;

            try
            {
                // 네이티브 쪽이 라이프사이클 순서를 관리한다.
                // C#은 전달받은 이벤트 번호를 실제 사용자 메서드 호출로만 바꾼다.
                switch (eventType)
                {
                    case 0: state.Instance.InvokeAwake(); break;
                    case 1: state.Instance.InvokeOnEnable(); break;
                    case 2: state.Instance.InvokeStart(); break;
                    case 3: state.Instance.InvokeFixedUpdate(deltaTime); break;
                    case 4: state.Instance.InvokeUpdate(deltaTime); break;
                    case 5: state.Instance.InvokeLateUpdate(deltaTime); break;
                    case 6: state.Instance.InvokeOnDisable(); break;
                    case 7: state.Instance.InvokeDestroy(); break;
                }
            }
            catch (Exception exception) { NativeApi.Log(exception.ToString()); }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void InvokePhysicsEventInstance(uint entityID, int eventType, uint otherEntityID)
        {
            if (!s_Instances.TryGetValue(entityID, out ScriptInstanceState? state))
                return;

            try { state.Instance.InvokePhysicsEvent(eventType, otherEntityID); }
            catch (Exception exception) { NativeApi.Log(exception.ToString()); }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void UpdateInstance(uint entityID, float deltaTime)
        {
            if (!s_Instances.TryGetValue(entityID, out ScriptInstanceState? state))
                return;

            try { state.Instance.InvokeUpdate(deltaTime); }
            catch (Exception exception) { NativeApi.Log(exception.ToString()); }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void Shutdown()
        {
            foreach (ScriptInstanceState state in s_Instances.Values)
            {
                try { state.Instance.InvokeDestroy(); }
                catch (Exception exception) { NativeApi.Log(exception.ToString()); }
            }
            s_Instances.Clear();
            s_GameAssembly = null;
            s_LoadContext?.Unload();
            s_LoadContext = null;
        }

        private static void ApplyFieldOverrides(CCEngine.GameScript instance, string? json)
        {
            if (string.IsNullOrWhiteSpace(json))
                return;

            using JsonDocument document = JsonDocument.Parse(json);
            if (document.RootElement.ValueKind != JsonValueKind.Object)
                return;

            Type type = instance.GetType();
            foreach (JsonProperty property in document.RootElement.EnumerateObject())
            {
                FieldInfo? field = type.GetField(property.Name, BindingFlags.Instance | BindingFlags.Public);
                if (field == null || field.IsInitOnly || field.IsLiteral)
                    continue;

                // 저장 파일에는 문자열로 보관하지만 실제 스크립트에는 필드 타입에 맞춰 다시 넣는다.
                if (field.FieldType == typeof(float) && property.Value.TryGetSingle(out float floatValue))
                    field.SetValue(instance, floatValue);
                else if (field.FieldType == typeof(int) && property.Value.TryGetInt32(out int intValue))
                    field.SetValue(instance, intValue);
                else if (field.FieldType == typeof(bool) && property.Value.ValueKind is JsonValueKind.True or JsonValueKind.False)
                    field.SetValue(instance, property.Value.GetBoolean());
                else if (field.FieldType == typeof(string))
                    field.SetValue(instance, property.Value.GetString() ?? string.Empty);
                else if (field.FieldType == typeof(CCEngine.Vector3) && property.Value.ValueKind == JsonValueKind.Object)
                {
                    float x = property.Value.TryGetProperty("X", out JsonElement xElement) && xElement.TryGetSingle(out float xv) ? xv : 0.0f;
                    float y = property.Value.TryGetProperty("Y", out JsonElement yElement) && yElement.TryGetSingle(out float yv) ? yv : 0.0f;
                    float z = property.Value.TryGetProperty("Z", out JsonElement zElement) && zElement.TryGetSingle(out float zv) ? zv : 0.0f;
                    field.SetValue(instance, new CCEngine.Vector3(x, y, z));
                }
            }
        }
    }
}
