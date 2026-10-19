global using AesCcm = Utilities.AesCcm;
using System.Net;
using SMBLibrary;
using SMBLibrary.Authentication.GSSAPI;
using SMBLibrary.Authentication.NTLM;
using SMBLibrary.Server;
using SMBLibrary.Win32;

// A test process on an ephemeral loopback port, with no Windows users or shares.
if (args.Length != 2 || !Directory.Exists(args[0]) ||
    !int.TryParse(args[1], out int port) || port < 1024 || port > 65535)
    throw new ArgumentException("Expected an existing test directory and an ephemeral port");
string user = Environment.GetEnvironmentVariable("FIXTURE_USER")
    ?? throw new ArgumentException("Missing fixture user");
string password = Environment.GetEnvironmentVariable("FIXTURE_PASSWORD")
    ?? throw new ArgumentException("Missing fixture password");
var authentication = new IndependentNTLMAuthenticationProvider(
    name => StringComparer.Ordinal.Equals(name, user) ? password : null);
var shares = new SMBShareCollection();
shares.Add(new FileSystemShare("files", new NTDirectoryFileSystem(Path.GetFullPath(args[0]))));
var server = new LoopbackServer(shares, new GSSProvider(authentication));
server.Start(port);
Console.WriteLine("SMB2 fixture ready");
using var stopped = new ManualResetEvent(false);
Console.CancelKeyPress += (_, e) => { e.Cancel = true; stopped.Set(); };
try { stopped.WaitOne(); }
finally { server.Stop(); }

sealed class LoopbackServer(SMBShareCollection shares, GSSProvider security)
    : SMBServer(shares, security)
{
    public void Start(int port) => Start(IPAddress.Loopback,
        SMBTransportType.DirectTCPTransport, port, false, true, false, null);
}
