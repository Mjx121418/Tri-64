import glob
import os
import sys

import common_compiler_flags


def has_osxcross():
    return "OSXCROSS_ROOT" in os.environ


def options(opts):
    opts.Add("macos_deployment_target", "macOS deployment target", "default")
    opts.Add("macos_sdk_path", "macOS SDK path", "")
    if has_osxcross():
        opts.Add("osxcross_sdk", "OSXCross SDK version", "darwin16")


def exists(env):
    return sys.platform == "darwin" or has_osxcross()


def find_host_llvm():
    requested = os.environ.get("OSXCROSS_HOST_LLVM")
    candidates = [requested] if requested else []
    candidates.extend(sorted(glob.glob("/usr/lib/llvm-*/bin"), reverse=True))
    candidates.extend(["/usr/local/bin", "/usr/bin"])
    for candidate in candidates:
        if candidate and os.path.isfile(os.path.join(candidate, "clang++")):
            return candidate
    return None


def generate(env):
    if env["arch"] not in ("universal", "arm64", "x86_64"):
        print("Only universal, arm64, and x86_64 are supported on macOS. Exiting.")
        env.Exit(1)

    if sys.platform == "darwin":
        env["CC"] = "clang"
        env["CXX"] = "clang++"
    else:
        root = os.environ.get("OSXCROSS_ROOT", "")
        host_llvm = find_host_llvm()
        if not host_llvm:
            raise RuntimeError("A compatible host LLVM installation is required for OSXCross")

        target_arch = "x86_64" if env["arch"] == "universal" else env["arch"]
        target = target_arch + "-apple-" + env["osxcross_sdk"]
        basecmd = root + "/target/bin/" + target + "-"
        env["CC"] = os.path.join(host_llvm, "clang")
        env["CXX"] = os.path.join(host_llvm, "clang++")
        env["LINK"] = os.path.join(host_llvm, "clang++")
        env["SHLINK"] = os.path.join(host_llvm, "clang++")
        if env["arch"] == "universal":
            # A normal ar archive cannot contain fat object members. libtool
            # creates one archive per architecture and combines the slices.
            env["AR"] = basecmd + "libtool"
            env["ARFLAGS"] = ["-static", "-o"]
            env["RANLIB"] = "true"
        else:
            env["AR"] = basecmd + "ar"
            env["RANLIB"] = basecmd + "ranlib"
        env["AS"] = basecmd + "as"
        env.PrependENVPath("PATH", root + "/target/bin")
        env.PrependENVPath("PATH", host_llvm)
        env.Append(CCFLAGS=["--target=" + target])
        env.Append(LINKFLAGS=["--target=" + target])

    if env["arch"] == "universal":
        env.Append(LINKFLAGS=["-arch", "x86_64", "-arch", "arm64"])
        env.Append(CCFLAGS=["-arch", "x86_64", "-arch", "arm64"])
    else:
        env.Append(LINKFLAGS=["-arch", env["arch"]])
        env.Append(CCFLAGS=["-arch", env["arch"]])

    if env["macos_deployment_target"] != "default":
        env.Append(CCFLAGS=["-mmacosx-version-min=" + env["macos_deployment_target"]])
        env.Append(LINKFLAGS=["-mmacosx-version-min=" + env["macos_deployment_target"]])

    if env["macos_sdk_path"]:
        env.Append(CCFLAGS=["-isysroot", env["macos_sdk_path"]])
        env.Append(LINKFLAGS=["-isysroot", env["macos_sdk_path"]])

    env.Append(CPPDEFINES=["MACOS_ENABLED", "UNIX_ENABLED"])

    if env["lto"] == "auto":
        env["lto"] = "none"

    common_compiler_flags.generate(env)
