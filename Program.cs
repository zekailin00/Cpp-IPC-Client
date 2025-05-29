using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;
using System.IO.MemoryMappedFiles;
using static System.Runtime.InteropServices.RuntimeInformation;

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
struct RpcRequest
{
    public int request_id;

    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
    public string function_name;

    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
    public string args_json;
}

[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
struct RpcResponse
{
    public int request_id;
    public int status_code;

    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
    public string result_json;
}

class Program
{
    const string RequestMap = "/MyRpcRequest";
    const string ResponseMap = "/MyRpcResponse";
    const string RequestSemaphore = "/MyRpcRequestSem";
    const string ResponseSemaphore = "/MyRpcResponseSem";

    static Dictionary<string, Func<Dictionary<string, object>, object>> functions = new()
    {
        // { "add", (args) => args["a"] + args["b"] },
        // { "sub", (args) => args["a"] - args["b"] }
        { "add", args => Convert.ToDouble(args["a"]) + Convert.ToDouble(args["b"]) },
        { "echo", args => args["text"]?.ToString() ?? "null" }
    };

    static void Main()
    {
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            RunWindows();
    }

    static void RunWindows()
    {
        var reqMMF = MemoryMappedFile.CreateOrOpen(RequestMap, 4096);
        var respMMF = MemoryMappedFile.CreateOrOpen(ResponseMap, 4096);
        var reqAcc = reqMMF.CreateViewAccessor();
        var respAcc = respMMF.CreateViewAccessor();
        var reqEvt = new EventWaitHandle(false, EventResetMode.AutoReset, RequestSemaphore);
        var respEvt = new EventWaitHandle(false, EventResetMode.AutoReset, ResponseSemaphore);

        while (true)
        {
            if (reqEvt.WaitOne(0)) // non-blocking poll
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
    }

    static Dictionary<string, object> ParseArgs(string json)
    {
        var args = new Dictionary<string, object>();
        using var doc = JsonDocument.Parse(json);
        foreach (var prop in doc.RootElement.EnumerateObject())
        {
            args[prop.Name] = ReadDynamicValue(prop.Value);
        }
        return args;
    }

    static object? ReadDynamicValue(JsonElement elem)
    {
        return elem.ValueKind switch {
            JsonValueKind.String => elem.GetString(),
            JsonValueKind.Number => elem.TryGetInt64(out var i) ? i : elem.GetDouble(),
            JsonValueKind.True => true,
            JsonValueKind.False => false,
            _ => null
        };
    }

    static string Dispatch(string func, string argsJson, out int status)
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
            Console.WriteLine($"[RPC Server] Error: {ex.Message}");
            status = 1;
            return JsonSerializer.Serialize(new { error = ex.Message });
        }
    }

    static T ReadStruct<T>(MemoryMappedViewAccessor acc) where T : struct
    {
        byte[] buffer = new byte[Marshal.SizeOf(typeof(T))];
        acc.ReadArray(0, buffer, 0, buffer.Length);
        GCHandle handle = GCHandle.Alloc(buffer, GCHandleType.Pinned);
        T result = (T)Marshal.PtrToStructure(handle.AddrOfPinnedObject(), typeof(T));
        handle.Free();
        return result;
    }

    static void WriteStruct<T>(MemoryMappedViewAccessor acc, T data) where T : struct
    {
        byte[] buffer = new byte[Marshal.SizeOf(typeof(T))];
        IntPtr ptr = Marshal.AllocHGlobal(buffer.Length);
        Marshal.StructureToPtr(data, ptr, true);
        Marshal.Copy(ptr, buffer, 0, buffer.Length);
        Marshal.FreeHGlobal(ptr);
        acc.WriteArray(0, buffer, 0, buffer.Length);
    }
}
