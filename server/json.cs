#if UNITY_2017_1_OR_NEWER
using UnityEngine; // Use Unity's JSON system
#else
using System.Text.Json;
#endif

using System;
using System.Threading;
using System.Collections.Generic;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;

namespace Serialization
{

    [Serializable]
    public class RpcArgsWrapper
    {
        public string[] keys;
        public string[] values;
    }

    [Serializable]
    public class ResultWrapper
    {
        public string result;
    }

    public class JsonHelper
    {
        static public string ToJson(string result)
        {
#if UNITY_2017_1_OR_NEWER
            return JsonUtility.ToJson(new ResultWrapper { result = result.ToString() });
#else
            return JsonSerializer.Serialize(
                new ResultWrapper { result = result.ToString() },
                new JsonSerializerOptions { IncludeFields = true }
            );
#endif
        }

        static public string ToJson(RpcArgsWrapper rpcArgs)
        {
#if UNITY_2017_1_OR_NEWER
            return JsonUtility.ToJson(rpcArgs);
#else
            return JsonSerializer.Serialize(rpcArgs,
                new JsonSerializerOptions { IncludeFields = true }
            );
#endif
        }

        static public RpcArgsWrapper? FromJson(string jsonString)
        {
#if UNITY_2017_1_OR_NEWER
            return JsonUtility.FromJson<RpcArgsWrapper>(jsonString);
#else
            return JsonSerializer.Deserialize<RpcArgsWrapper>(
                jsonString,
                new JsonSerializerOptions { IncludeFields = true }
            );
#endif
        }
    }
}