// Program.cs — scrip-interp entry point
//
// Usage: dotnet run -- <file.sno>
//        dotnet run -- <file.sno> [--trace]
//
// Exit: 0 = normal END, 1 = error
//
// AUTHORS: Lon Jones Cherryholmes · Claude Sonnet 4.6
// SPRINT:  M-NET-INTERP-A01

using ScripInterp;

if (args.Length < 1)
{
    Console.Error.WriteLine("usage: scrip-interp <file.sno>");
    return 1;
}

var path = args[0];
if (!File.Exists(path))
{
    Console.Error.WriteLine($"scrip-interp: file not found: {path}");
    return 1;
}

try
{
    var program = Snobol4Parser.ParseFile(path);
    var env     = new SnobolEnv();
    var exec    = new Executor(env);
    exec.Run(program);
    return 0;
}
catch (Exception ex)
{
    Console.Error.WriteLine($"scrip-interp: {ex.Message}");
    return 1;
}
