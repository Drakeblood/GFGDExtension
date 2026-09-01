#!/usr/bin/env python
import os
import sys

from methods import print_error


libname = "GFGD"
projectdir = "project"
addondir = "addons/gfgdextension"

localEnv = Environment(tools=["default"], PLATFORM="")

# Build profiles can be used to decrease compile times.
# You can either specify "disabled_classes", OR
# explicitly specify "enabled_classes" which disables all other classes.
# Modify the example file as needed and uncomment the line below or
# manually specify the build_profile parameter when running SCons.

# localEnv["build_profile"] = "build_profile.json"

customs = ["custom.py"]
customs = [os.path.abspath(path) for path in customs]

opts = Variables(customs, ARGUMENTS)
opts.Update(localEnv)

Help(opts.GenerateHelpText(localEnv))

env = localEnv.Clone()

if not (os.path.isdir("godot-cpp") and os.listdir("godot-cpp")):
    print_error("""godot-cpp is not available within this folder, as Git submodules haven't been initialized.
Run the following command to download godot-cpp:

    git submodule update --init --recursive""")
    sys.exit(1)

env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})

env.Append(CPPPATH=["src/"])
# Sources are grouped into per-domain folders under src/, so collect them
# recursively; includes are written relative to src/, which CPPPATH covers.
# src/gen is skipped: the generated doc_data.gen.cpp is appended below by the
# GodotCPPDocData builder, and listing it twice would duplicate the source.
source_dirs = [d.replace(os.sep, "/") for d, _, _ in os.walk("src") if "gen" not in d.split(os.sep)]
sources = [f for d in source_dirs for f in Glob(d + "/*.cpp")]

if env["target"] in ["editor", "template_debug"]:
    try:
        doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
        sources.append(doc_data)
    except AttributeError:
        print("Not including class reference as we're targeting a pre-4.3 baseline.")

# .dev doesn't inhibit compatibility, so we don't need to key it.
# .universal just means "compatible with all relevant arches" so we don't need to key it.
suffix = env['suffix'].replace(".dev", "").replace(".universal", "")

lib_filename = "{}{}{}{}".format(env.subst('$SHLIBPREFIX'), libname, suffix, env.subst('$SHLIBSUFFIX'))

library = env.SharedLibrary(
    "bin/{}/{}".format(env['platform'], lib_filename),
    source=sources,
)

# Only the shared library belongs in the addon. MSVC also emits an import
# library and an export file next to it, which are link-time artifacts - they
# would otherwise be copied in and end up in a release.
shared_lib_suffix = env.subst("$SHLIBSUFFIX")
redistributable = [node for node in library if str(node).endswith(shared_lib_suffix)]

copy = env.Install("{}/{}/bin/{}/".format(projectdir, addondir, env["platform"]), redistributable)

default_args = [library, copy]
Default(*default_args)
