const std = @import("std");

pub fn main() !void {
    const args = std.process.args();

    for (args) |arg| {
        if (std.mem.eql(u8, arg, "--version") or std.mem.eql(u8, arg, "-v")) {
            std.debug.print("Fn version 0.0.1\n");
            return;
        } else if (std.mem.eql(u8, arg, "--help") or std.mem.eql(u8, arg, "-h")) {
            std.debug.print("Usage: fn [OPTION]\n");
            std.debug.print("\n");
            std.debug.print("Options:\n");
            std.debug.print("  --version, -v  Display version information and exit\n");
            std.debug.print("  --help, -h     Display this help and exit\n");
            return;
        } else {
            std.debug.print("Unknown option: {}\n", arg);
            return;
        }
    }
}
