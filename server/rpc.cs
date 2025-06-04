using System;
using System.Threading;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Serialization;
using System.Net.Sockets;
using System.Net;
using System.Text;
using System.IO;
using System.Collections.Concurrent;
using System.Threading.Tasks;

#if UNITY_2017_1_OR_NEWER
using UnityEngine;
#endif


namespace SharedMemRPC
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    public struct RpcRequest
    {
        public int request_id;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string functionName;

        public int bufferSize;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi, Pack = 1)]
    public struct ResponseHeader
    {
        // enum class MsgType {
        //     CALLBACK = 0,
        //     RETURN = 1
        // };
        public int clientId;
        public int msgType;
        public int statusCodeOrCallbackId;
        public int bufferSize;
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
        private readonly Dictionary<int, TcpClient> clients = new();
        private readonly Dictionary<int, int> callbackToClientId = new();
        private readonly List<Thread> threads = new();
        private readonly ConcurrentQueue<Action> mainThreadQueue = new();

        private int nextCallbackId = 0;
        private readonly Mutex respMutex = new();
        private readonly Mutex queueMutex = new();

        private readonly TcpListener listener;

        public HandleRegistry handleRegistry;

        public RpcServer(int port = 6969)
        {
            handleRegistry = new HandleRegistry();
            Register<Func<int, int>>("_RPC::AllocateCallback", (int clientId) =>
            {
                callbackToClientId[nextCallbackId] = clientId;
                return nextCallbackId++;
            });

            listener = new TcpListener(IPAddress.Any, port);
            listener.Start();
            listener.BeginAcceptTcpClient(OnClientConnected, null);
            DebugPrint("Server started...");
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

        public void ProcessRPC()
        {
            queueMutex.WaitOne();
            while (mainThreadQueue.TryDequeue(out Action action))
            {
                queueMutex.ReleaseMutex();
                try
                {
                    action();
                }
                catch (Exception ex)
                {
                    DebugPrint($"[RPC Service {Environment.CurrentManagedThreadId}] Unexpected error: {ex}");
                }
                queueMutex.WaitOne();
            }
            queueMutex.ReleaseMutex();
        }

        private void OnClientConnected(IAsyncResult ar)
        {
            TcpClient client = listener.EndAcceptTcpClient(ar);
            var thread = new Thread(() => HandleClient(client));
            threads.Add(thread);
            thread.Start();

            // Continue accepting new clients
            listener.BeginAcceptTcpClient(OnClientConnected, null);
        }

        private void OnClientDisconnected(int clientId)
        {
            DebugPrint($"[RPC Service {clientId}] Disconnected callback triggered.");
        }

        public void WaitAllClients()
        {
            foreach (Thread t in threads)
            {
                t.Join();
            }
        }

        public void RunOnMainThread(Action action)
        {
            queueMutex.WaitOne();
            mainThreadQueue.Enqueue(action);
            queueMutex.ReleaseMutex();
        }

        private void HandleClient(TcpClient client)
        {
            try
            {
                respMutex.WaitOne();
                clients[Environment.CurrentManagedThreadId] = client;
                respMutex.ReleaseMutex();
                client.NoDelay = true;
                DebugPrint("Client connected.");

                NetworkStream networkStream = client.GetStream();
                WriteHeader(networkStream, new ResponseHeader
                {
                    clientId = Environment.CurrentManagedThreadId,
                    msgType = 1,
                    statusCodeOrCallbackId = 0,
                    bufferSize = 0
                });

                while (true)
                {
                    DebugPrint($"[RPC Service {Environment.CurrentManagedThreadId}] Waiting for request...");
                    var req = ReadHeader<RpcRequest>(networkStream);
                    string argsJson = ReadPayload(networkStream, req.bufferSize);
                    int clientId = Environment.CurrentManagedThreadId;

                    RunOnMainThread(() =>
                    {
                        string result = Dispatch(clientId, req.functionName, argsJson, out int status);

                        var resp = new ResponseHeader
                        {
                            clientId = clientId,
                            msgType = 1,
                            statusCodeOrCallbackId = status,
                            bufferSize = result.Length
                        };

                        respMutex.WaitOne();
                        if (clients.TryGetValue(clientId, out TcpClient tcpClient) && tcpClient.Connected)
                        {
                            WriteHeader(tcpClient.GetStream(), resp);
                            WritePayload(tcpClient.GetStream(), result);
                        }
                        respMutex.ReleaseMutex();

                        DebugPrint($"[RPC Service {clientId}] Handled: {req.functionName}, Response: {result}");
                    });
                }
            }
            catch (IOException)
            {
                respMutex.WaitOne();
                clients.Remove(Environment.CurrentManagedThreadId);
                client.Close();
                respMutex.ReleaseMutex();
                
                DebugPrint($"[RPC Service {Environment.CurrentManagedThreadId}] Client disconnected.");
                OnClientDisconnected(Environment.CurrentManagedThreadId);
            }
            catch (Exception ex)
            {
                DebugPrint($"[RPC Service {Environment.CurrentManagedThreadId}] Unexpected error: {ex}");
            }
            // finally
            // {
            //     respMutex.WaitOne();
            //     clients.Remove(Environment.CurrentManagedThreadId);
            //     client.Close();
            //     respMutex.ReleaseMutex();
            // }
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

            var cb = new ResponseHeader
            {
                clientId = callbackToClientId[callbackId],
                msgType = 0,
                statusCodeOrCallbackId = callbackId,
                bufferSize = json.Length
            };

            respMutex.WaitOne();
            if (clients.TryGetValue(cb.clientId, out TcpClient tcpClient) && tcpClient.Connected)
            {
                WriteHeader(tcpClient.GetStream(), cb);
                WritePayload(tcpClient.GetStream(), json);
            }
            respMutex.ReleaseMutex();
        }

        private string Dispatch(int clientId, string func, string argsJson, out int status)
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
                DebugPrint(
                    $"[RPC Service {clientId}] Dispatch exception: "+
                    ex.Message + "\n" + ex.StackTrace
                );
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

        private static T ReadHeader<T>(NetworkStream stream) where T : struct
        {
            int size = Marshal.SizeOf<T>();
            byte[] buffer = new byte[size];
            int read = stream.Read(buffer, 0, size);
            if (read != size)
                throw new IOException("Failed to read full header");

            GCHandle handle = GCHandle.Alloc(buffer, GCHandleType.Pinned);
            T header = Marshal.PtrToStructure<T>(handle.AddrOfPinnedObject());
            handle.Free();
            return header;
        }

        private static string ReadPayload(NetworkStream stream, int size)
        {
            byte[] buffer = new byte[size];
            int readTotal = 0;
            while (readTotal < size)
            {
                int read = stream.Read(buffer, readTotal, size - readTotal);
                if (read <= 0) throw new IOException("Failed to read full payload");
                readTotal += read;
            }
            return Encoding.UTF8.GetString(buffer);
        }

        private static void WriteHeader<T>(NetworkStream stream, T header) where T : struct
        {
            int size = Marshal.SizeOf<T>();
            byte[] buffer = new byte[size];
            GCHandle handle = GCHandle.Alloc(buffer, GCHandleType.Pinned);
            Marshal.StructureToPtr(header, handle.AddrOfPinnedObject(), false);
            handle.Free();
            stream.Write(buffer, 0, size);
        }

        private static void WritePayload(NetworkStream stream, string data)
        {
            byte[] buffer = Encoding.UTF8.GetBytes(data);
            stream.Write(buffer, 0, buffer.Length);
        }

        private static void DebugPrint(string message)
        {
#if UNITY_2017_1_OR_NEWER
            Debug.Log(message);
#else
            Console.WriteLine(message);
#endif
        }
    }
}
