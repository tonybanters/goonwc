const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const root_module = b.createModule(.{
        .target = target,
        .optimize = optimize,
    });

    const exe = b.addExecutable(.{
        .name = "dwc",
        .root_module = root_module,
    });

    exe.addCSourceFiles(.{
        .files = &.{
            "src/main.c",
            "src/server.c",
        },
        .flags = &.{
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-D_POSIX_C_SOURCE=200809L",
        },
    });

    exe.addIncludePath(b.path("include"));
    exe.addIncludePath(b.path("../owl/include"));

    exe.addLibraryPath(b.path("../owl/lib"));
    exe.linkSystemLibrary("owl");

    exe.linkLibC();
    exe.linkSystemLibrary("wayland-server");
    exe.linkSystemLibrary("xkbcommon");
    exe.linkSystemLibrary("drm");
    exe.linkSystemLibrary("gbm");
    exe.linkSystemLibrary("EGL");
    exe.linkSystemLibrary("GLESv2");
    exe.linkSystemLibrary("input");
    exe.linkSystemLibrary("libudev");

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run the compositor");
    run_step.dependOn(&run_cmd.step);

    const clean_step = b.step("clean", "Remove build artifacts");
    const clean_cmd = b.addSystemCommand(&.{
        "rm", "-rf", "zig-out", ".zig-cache",
    });
    clean_step.dependOn(&clean_cmd.step);
}
