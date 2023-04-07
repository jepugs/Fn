const std = @import("std");
const eql = std.mem.eql;
const devtest = @import("devtest.zig");

pub fn main() !void {
    var args = std.process.args();
    // skip over executable name
    _ = args.skip();

    var verb: []const u8 = "help";
    while (args.next()) |arg| {
        if (eql(u8, arg, "--version") or eql(u8, arg, "-v")) {
            printVersion();
            return;
        } else if (eql(u8, arg, "--help") or eql(u8, arg, "-h")) {
            printUsage();
            return;
        } else if (arg.len > 0 and arg[0] != '-') {
            verb = arg;
            break;
        } else {
            std.debug.print("Unknown option: {s}\n", .{arg});
            return;
        }
    }

    if (eql(u8, verb, "build")) {
        std.debug.print("Unimplemented\n", .{});
        return;
    } else if (eql(u8, verb, "devtest")) {
        devtest.do_devtest(&args);
        return; 
    } else if (eql(u8, verb, "help")) {
        printUsage();
        return;
    } else if (eql(u8, verb, "init")) {
        std.debug.print("Unimplemented\n", .{});
        return;
    } else if (eql(u8, verb, "repl")) {
        std.debug.print("Unimplemented\n", .{});
        return;
    } else if (eql(u8, verb, "script")) {
        std.debug.print("Unimplemented\n", .{});
        return;
    } else {
        std.debug.print("Unknown verb: {s}\n", .{verb});
        return;
    }
}

pub fn echo(comptime str: []const u8) void {
    std.debug.print(str, .{});
}

pub fn printUsage() void {
    echo("Usage: fn [OPTIONS] VERB ARGS ...\n");
    echo("\n");
    echo("Options:\n");
    echo("  --version, -v  Display version information and exit\n");
    echo("  --help, -h     Display this help and exit\n");
    echo("\n");
    echo("Verbs:\n");
    echo("  build          Build an Fn project (unimplemented)\n");
    echo("  help           Display this help\n");
    echo("  init           Initialize a new Fn project (unimplemented)\n");
    echo("  repl           Open a REPL (unimplemented)\n");
    echo("  script         Run an Fn script (unimplemented)\n");
    echo("\n");
    echo("For further information, try `fn VERB help`.\n");
    return;
}

pub fn printVersion() void {
    echo("Fn version 0.0.1\n");
}
