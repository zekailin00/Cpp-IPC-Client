using SharedMemRPC;
using System.Threading;

class Program
{
    static float mul(float a, float b)
    {
        return a * b;
    }
    
    static void Main()
    {
        RpcServer server = new RpcServer();

        server.Register("add", (float a, float b) => a + b);
        server.Register("sub", (double a, double b) => a - b);
        server.Register("mul", mul);
        server.Register("echo", args => args["text"]?.ToString() ?? "null");

        server.Register("timer", (args) =>
        {
            return server.handleRegistry.AddHandle(new Timer(
                callback: state => server.TriggerCallback(int.Parse(args["id"]), "{}"),                                  // the method to call
                state: null,                        // optional state object
                dueTime: 0,                         // delay before first call (ms)
                period: int.Parse(args["interval"])                        // repeat interval (ms)
            ));
        });

        server.Register("dispose_timer", args =>
        {
            int id = int.Parse(args["timer_id"]);
            if (server.handleRegistry.GetObject(id, out Timer timer))
            {
                timer.Dispose();
                server.handleRegistry.RemoveHandle(id);
                return true;
            }
            return false;
        });

        server.Start(); // blocking call
    }
}