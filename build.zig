const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const raylib_dep = b.dependency("raylib", .{
        .target = target,
        .optimize = optimize,
    });

    // TODO: these resources should all just be embedded inside
    //       the executable
    b.installDirectory(.{
        .source_dir = .{ .path = "resources" },
        .install_dir = .prefix,
        .install_subdir = "resources",
    });
    const exe = b.addExecutable(.{
        .name = "musializer",
        .target = target,
        .optimize = optimize,
    });
    // TODO: add the ".rc" file (does zig support this?)
    exe.addCSourceFiles(&.{
        "src/musializer.c",
        "src/plug.c",
        switch (target.getOs().tag) {
            .windows => "src/ffmpeg_windows.c",
            else => "src/ffmpeg_linux.c",
        }
    }, &.{
        "-Wall",
        "-Wextra",
    });
    exe.linkLibrary(raylib_dep.artifact("raylib"));
    exe.linkLibC();

    b.installArtifact(exe);
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }
    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);
}
