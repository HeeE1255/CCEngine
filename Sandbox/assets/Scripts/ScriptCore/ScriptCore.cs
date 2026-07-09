#nullable enable
using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace CCEngine
{
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

        public void Log(string message) => Internal.NativeApi.Log(message);

        protected virtual void OnCreate() { }
        protected virtual void OnUpdate(float deltaTime) { }
        protected virtual void OnDestroy() { }

        internal void InvokeCreate() => OnCreate();
        internal void InvokeUpdate(float deltaTime) => OnUpdate(deltaTime);
        internal void InvokeDestroy() => OnDestroy();
    }
}

namespace CCEngine.Internal
{
    internal static unsafe class NativeApi
    {
        private static delegate* unmanaged[Cdecl]<uint, float*, float*, float*, int> s_GetTranslation;
        private static delegate* unmanaged[Cdecl]<uint, float, float, float, void> s_SetTranslation;
        private static delegate* unmanaged[Cdecl]<byte*, void> s_Log;

        internal static void Bind(nint getTranslation, nint setTranslation, nint log)
        {
            s_GetTranslation = (delegate* unmanaged[Cdecl]<uint, float*, float*, float*, int>)getTranslation;
            s_SetTranslation = (delegate* unmanaged[Cdecl]<uint, float, float, float, void>)setTranslation;
            s_Log = (delegate* unmanaged[Cdecl]<byte*, void>)log;
        }

        internal static CCEngine.Vector3 GetTranslation(uint entityID)
        {
            float x = 0, y = 0, z = 0;
            s_GetTranslation(entityID, &x, &y, &z);
            return new CCEngine.Vector3(x, y, z);
        }

        internal static void SetTranslation(uint entityID, CCEngine.Vector3 value)
            => s_SetTranslation(entityID, value.X, value.Y, value.Z);

        internal static void Log(string message)
        {
            byte[] utf8 = System.Text.Encoding.UTF8.GetBytes(message + '\0');
            fixed (byte* text = utf8)
                s_Log(text);
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
        private static readonly Dictionary<uint, CCEngine.GameScript> s_Instances = new();
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
        public static int CreateInstance(uint entityID, nint className)
        {
            try
            {
                string? name = Marshal.PtrToStringUTF8(className);
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
                s_Instances[entityID] = instance;
                instance.InvokeCreate();
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
            if (!s_Instances.Remove(entityID, out CCEngine.GameScript? instance))
                return;

            try { instance.InvokeDestroy(); }
            catch (Exception exception) { NativeApi.Log(exception.ToString()); }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void UpdateInstance(uint entityID, float deltaTime)
        {
            if (!s_Instances.TryGetValue(entityID, out CCEngine.GameScript? instance))
                return;

            try { instance.InvokeUpdate(deltaTime); }
            catch (Exception exception) { NativeApi.Log(exception.ToString()); }
        }

        [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
        public static void Shutdown()
        {
            foreach (CCEngine.GameScript instance in s_Instances.Values)
            {
                try { instance.InvokeDestroy(); }
                catch (Exception exception) { NativeApi.Log(exception.ToString()); }
            }
            s_Instances.Clear();
            s_GameAssembly = null;
            s_LoadContext?.Unload();
            s_LoadContext = null;
        }
    }
}
