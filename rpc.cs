using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Text.Json;

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

    public class RpcServer
    {
        private readonly Dictionary<string, Func<Dictionary<string, object>, object>> functions = new();

        private readonly MemoryMappedFile reqMMF;
        private readonly MemoryMappedFile respMMF;
        private readonly MemoryMappedViewAccessor reqAcc;
        private readonly MemoryMappedViewAccessor respAcc;
        private readonly EventWaitHandle reqEvt;
        private readonly EventWaitHandle respEvt;

        public RpcServer(
            string requestMap = "/MyRpcRequest",
            string responseMap = "/MyRpcResponse",
            string requestEvent = "/MyRpcRequestSem",
            string responseEvent = "/MyRpcResponseSem")
        {
            reqMMF = MemoryMappedFile.CreateOrOpen(requestMap, 4096);
            respMMF = MemoryMappedFile.CreateOrOpen(responseMap, 4096);
            reqAcc = reqMMF.CreateViewAccessor();
            respAcc = respMMF.CreateViewAccessor();
            reqEvt = new EventWaitHandle(false, EventResetMode.AutoReset, requestEvent);
            respEvt = new EventWaitHandle(false, EventResetMode.AutoReset, responseEvent);
        }

        public void Register(string name, Func<Dictionary<string, object>, object> handler)
        {
            functions[name] = handler;
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
                Console.WriteLine($"[RPC Server] Handled: {req.function_name}, Args: {req.args_json}");
            }
        }

        private string Dispatch(string func, string argsJson, out int status)
        {
            try
            {
                var args = ParseArgs(argsJson);
                var result = functions[func](args);
                status = 0;
                return JsonSerializer.Serialize(result);
            }
            catch (Exception ex)
            {
                status = 1;
                return JsonSerializer.Serialize(new { error = ex.Message });
            }
        }

        private Dictionary<string, object> ParseArgs(string json)
        {
            var args = new Dictionary<string, object>();
            using var doc = JsonDocument.Parse(json);
            foreach (var prop in doc.RootElement.EnumerateObject())
            {
                args[prop.Name] = ReadDynamicValue(prop.Value);
            }
            return args;
        }

        private object? ReadDynamicValue(JsonElement elem)
        {
            return elem.ValueKind switch
            {
                JsonValueKind.String => elem.GetString(),
                JsonValueKind.Number => elem.TryGetInt64(out var i) ? i : elem.GetDouble(),
                JsonValueKind.True => true,
                JsonValueKind.False => false,
                _ => null
            };
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
