using System;
using System.Threading;
using System.Collections.Generic;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using Serialization;

namespace SharedMemRPC
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    public struct RpcRequest
    {
        public int request_id;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string function_name;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
        public string args_json;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    public struct RpcResponse
    {
        public int request_id;
        public int status_code;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
        public string result_json;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    public struct RpcCallback
    {
        public int callback_id;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
        public string args_json;
    }

    public class HandleRegistry
    {
        private readonly Dictionary<int, object> handles = new();
        private int nextHandleId = 1;

        public int AddHandle(object obj)
        {
            int handleId = nextHandleId++;
            handles[handleId] = obj;
            return handleId;
        }

        public bool GetObject<T>(int handleID, out T obj)
        {
            bool found = handles.TryGetValue(handleID, out object? value);
            obj = (T) value;
            return found;
        }

        public bool RemoveHandle(int handle)
        {
            return handles.Remove(handle);
        }
    }

    public class RpcServer
    {
        private readonly Dictionary<string, Func<Dictionary<string, string>, object>> functions = new();

        private readonly MemoryMappedFile reqMMF;
        private readonly MemoryMappedFile respMMF;
        private readonly MemoryMappedFile cbMMF;
        private readonly MemoryMappedViewAccessor reqAcc;
        private readonly MemoryMappedViewAccessor respAcc;
        private readonly MemoryMappedViewAccessor cbAcc;
        private readonly EventWaitHandle reqEvt;
        private readonly EventWaitHandle respEvt;
        private readonly EventWaitHandle cbEvt;

        public HandleRegistry handleRegistry;

        public RpcServer(
            string requestMap = "/MyRpcRequest",
            string responseMap = "/MyRpcResponse",
            string callbackMap = "/MyRpcCallback",
            string requestEvent = "/MyRpcRequestSem",
            string responseEvent = "/MyRpcResponseSem",
            string callbackEvent = "/MyRpcCallbackSem")
        {
            reqMMF = MemoryMappedFile.CreateOrOpen(requestMap, 4096);
            respMMF = MemoryMappedFile.CreateOrOpen(responseMap, 4096);
            cbMMF = MemoryMappedFile.CreateOrOpen(callbackMap, 4096);
            reqAcc = reqMMF.CreateViewAccessor();
            respAcc = respMMF.CreateViewAccessor();
            cbAcc = cbMMF.CreateViewAccessor();
            reqEvt = new EventWaitHandle(false, EventResetMode.AutoReset, requestEvent);
            respEvt = new EventWaitHandle(false, EventResetMode.AutoReset, responseEvent);
            cbEvt = new EventWaitHandle(false, EventResetMode.AutoReset, callbackEvent);

            handleRegistry = new HandleRegistry();
        }

        public void Register<TDelegate>(string name, TDelegate del) where TDelegate : Delegate
        {
            var method = del.Method;
            var target = del.Target;

            functions[name] = (argDict) =>
            {
                var parameters = method.GetParameters();
                var args = new object[parameters.Length];

                for (int i = 0; i < parameters.Length; i++)
                {
                    var param = parameters[i];
                    if (!argDict.TryGetValue(param.Name, out var strValue))
                        throw new ArgumentException($"Missing argument: {param.Name}");

                    args[i] = Convert.ChangeType(strValue, param.ParameterType);
                }

                object? result = method.Invoke(target, args);
                return result?.ToString() ?? "";
            };
        }

        public void Start()
        {
            while (true)
            {
                ProcessRPC();
            }
        }

        public void ProcessRPC()
        {
            if (reqEvt.WaitOne(0))
            {
                RpcRequest req = ReadStruct<RpcRequest>(reqAcc);
                string result = Dispatch(req.function_name, req.args_json, out int status);

                RpcResponse resp = new RpcResponse
                {
                    request_id = req.request_id,
                    status_code = status,
                    result_json = result
                };

                WriteStruct(respAcc, resp);
                respEvt.Set();
                // Debug.Log($"[RPC Server] Handled: {req.function_name}, Args: {req.args_json}");
                Console.WriteLine($"[RPC Server] Handled: {req.function_name}, Args: {req.args_json}");
            }
        }

        public void TriggerCallback(int callbackId, string argsJson)
        {
            RpcCallback cb = new RpcCallback
            {
                callback_id = callbackId,
                args_json = argsJson
            };
            WriteStruct(cbAcc, cb);
            cbEvt.Set();
        }

        public void TriggerCallback(int callbackId, object namedArgs)
        {
            var props = namedArgs.GetType().GetProperties();
            var keys = new List<string>();
            var values = new List<string>();

            foreach (var prop in props)
            {
                keys.Add(prop.Name);
                object value = prop.GetValue(namedArgs);
                values.Add(value?.ToString() ?? "");
            }

            var wrapper = new RpcArgsWrapper
            {
                keys = keys.ToArray(),
                values = values.ToArray()
            };

            string json = JsonHelper.ToJson(wrapper);

            var cb = new RpcCallback
            {
                callback_id = callbackId,
                args_json = json
            };

            WriteStruct(cbAcc, cb);
            cbEvt.Set();
        }

        private string Dispatch(string func, string argsJson, out int status)
        {
            try
            {
                Dictionary<string, string> args = ParseArgs(argsJson);
                var result = functions[func](args);
                status = 0;
                return JsonHelper.ToJson(result?.ToString());
            }
            catch (Exception ex)
            {
                // Debug.Log("Dispatch Exception");
                // Debug.Log(ex.Message);
                Console.WriteLine("Dispatch exception: " + ex.Message);
                status = 1;
                return JsonHelper.ToJson(ex.Message);
            }
        }

        private Dictionary<string, string> ParseArgs(string json)
        {
            RpcArgsWrapper? parsed = JsonHelper.FromJson(json);

            var dict = new Dictionary<string, string>();
            for (int i = 0; i < parsed?.keys.Length; i++)
            {
                dict[parsed.keys[i]] = parsed.values[i];
            }

            return dict;
        }

        private static T ReadStruct<T>(MemoryMappedViewAccessor acc) where T : struct
        {
            byte[] buffer = new byte[Marshal.SizeOf(typeof(T))];
            acc.ReadArray(0, buffer, 0, buffer.Length);
            GCHandle handle = GCHandle.Alloc(buffer, GCHandleType.Pinned);
            T result = Marshal.PtrToStructure<T>(handle.AddrOfPinnedObject());
            handle.Free();
            return result;
        }

        private static void WriteStruct<T>(MemoryMappedViewAccessor acc, T data) where T : struct
        {
            byte[] buffer = new byte[Marshal.SizeOf(typeof(T))];
            IntPtr ptr = Marshal.AllocHGlobal(buffer.Length);
            Marshal.StructureToPtr(data, ptr, true);
            Marshal.Copy(ptr, buffer, 0, buffer.Length);
            Marshal.FreeHGlobal(ptr);
            acc.WriteArray(0, buffer, 0, buffer.Length);
        }
    }
}
