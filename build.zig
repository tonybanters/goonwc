const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const owl_path = std.process.getEnvVarOwned(b.allocator, "OWL_PATH") catch {
        std.debug.print("Error: OWL_PATH environment variable not set.\n", .{});
        std.debug.print("Run 'nix develop' first, or set OWL_PATH manually.\n", .{});
        return;
    };

    const root_module = b.createModule(.{
        .target = target,
        .optimize = optimize,
    });

    const exe = b.addExecutable(.{
        .name = "dwc",
        .root_module = root_module,
    });

    const cflags = &.{
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
        "-D_POSIX_C_SOURCE=200809L",
    };

    const xdg_shell_xml = b.fmt("{s}/protocols/xdg-shell.xml", .{owl_path});

    const gen_header = b.addSystemCommand(&.{
        "wayland-scanner", "server-header", xdg_shell_xml,
    });
    const xdg_header = gen_header.addOutputFileArg("xdg-shell-protocol.h");

    const gen_code = b.addSystemCommand(&.{
        "wayland-scanner", "private-code", xdg_shell_xml,
    });
    const xdg_code = gen_code.addOutputFileArg("xdg-shell-protocol.c");

    exe.addCSourceFiles(.{
        .files = &.{
            "src/main.c",
            "src/server.c",
        },
        .flags = cflags,
    });

    const owl_src = b.fmt("{s}/src", .{owl_path});
    exe.addCSourceFiles(.{
        .root = .{ .cwd_relative = owl_src },
        .files = &.{
            "callbacks.c",
            "display.c",
            "input.c",
            "layer_shell.c",
            "output.c",
            "render.c",
            "surface.c",
            "xdg_shell.c",
        },
        .flags = cflags,
    });

    exe.addIncludePath(b.path("include"));
    exe.addIncludePath(.{ .cwd_relative = b.fmt("{s}/include", .{owl_path}) });
    exe.addIncludePath(.{ .cwd_relative = b.fmt("{s}/src", .{owl_path}) }); 
    exe.addIncludePath(xdg_header.dirname()); 
    exe.addIncludePath(xdg_code.dirname()); 

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
