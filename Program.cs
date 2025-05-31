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
        server.Register("echo", (string text) => "Server echo: " + text);

        server.Register("do_work", (string input, int delay, int onComplete) =>
        {
            Console.WriteLine("input: " + input);
            Console.WriteLine("Delay: " + delay);
            Console.WriteLine("onComoplate: " + onComplete);
            server.TriggerCallback(onComplete, new { a = 1, b = "str", c = 3.33 });
        });

        server.Register("timer", (int interval, int callback, string text) =>
        {
            return server.handleRegistry.AddHandle(new Timer(
                callback: state => server.TriggerCallback(callback, new { text }),
                state: null,
                dueTime: 0,
                period: interval
            ));
        });

        server.Register("dispose_timer", (int timerHandle) =>
        {
            Console.WriteLine("Handle disposed: " + timerHandle);
            if (server.handleRegistry.GetObject(timerHandle, out Timer timer))
            {
                timer.Dispose();
                server.handleRegistry.RemoveHandle(timerHandle);
                return true;
            }
            return false;
        });

        server.Start(); // blocking call
    }
}