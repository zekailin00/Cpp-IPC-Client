using SharedMemRPC;

class Program
{
    static void Main()
    {
        RpcServer server = new RpcServer();

        server.Register("add", args => Convert.ToDouble(args["a"]) + Convert.ToDouble(args["b"]));
        server.Register("sub", args => Convert.ToDouble(args["a"]) - Convert.ToDouble(args["b"]));
        server.Register("mul", args => Convert.ToDouble(args["a"]) * Convert.ToDouble(args["b"]));
        server.Register("echo", args => args["text"]?.ToString() ?? "null");

        server.Start(); // blocking call
    }
}