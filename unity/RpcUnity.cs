using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using SharedMemRPC;
using System;

public class RpcUnity : MonoBehaviour
{
    RpcServer server;

    // Start is called before the first frame update
    void Start()
    {
        server = new RpcServer();
        server.Register("add", args => Convert.ToDouble(args["a"]) + Convert.ToDouble(args["b"]));
        server.Register("sub", args =>
        {
            Debug.Log("Sub entered");
            return Convert.ToDouble(args["a"]) - Convert.ToDouble(args["b"]);
            
            });
        server.Register("mul", args => Convert.ToDouble(args["a"]) * Convert.ToDouble(args["b"]));
        server.Register("echo", args => args["text"]?.ToString() ?? "null");

    }

    // Update is called once per frame
    void Update()
    {
        server.ProcessRPC();
    }
}
