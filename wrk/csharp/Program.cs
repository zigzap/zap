var builder = WebApplication.CreateBuilder(args);
builder.Logging.ClearProviders();

var app = builder.Build();

app.MapGet("/", () => "Hello from C#1234");

app.Run();
