const std = @import("std");
const mem = std.mem;
const eql = mem.eql;

pub fn do_devtest(args: *std.process.ArgIterator) void {
    if (args.next()) |arg| {
        if (eql(u8, arg, "help")) {
            printUsage();
            return;
        }
        devtest_dispatch(arg, args);
    } else {
        printUsage();
        return;
    }
}

fn printUsage() void {
    std.debug.print("Usage: devtest help\n", .{});
    std.debug.print("       devtest <test-id> <test-args>\n", .{});
    std.debug.print("Runs internal diagnostics. This will not be of interest to most users.\n", .{});
}

const DevTest = struct {
    id: []const u8,
    func: *const fn (*std.process.ArgIterator) void,
};

fn devtest_dispatch(testId: []const u8, args: *std.process.ArgIterator) void {
    // find the test function
    for (devtest_tests) |t| {
        if (eql(u8, t.id, testId)) {
            t.func(args);
            return;
        }
    }
    std.debug.print("Unknown test id: {s}\n", .{testId});
}

fn test1(args: *std.process.ArgIterator) void {
    _ = args;
    std.debug.print("test1\n", .{});
}

// array of function pointers
const devtest_tests = [_]DevTest{
    DevTest{ .id = "1", .func = test1 },
};


