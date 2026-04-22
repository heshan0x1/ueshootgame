import os
import sys
import stat
import shutil
import tempfile
import threading
import subprocess
import pathlib

# Path setup
source_path  = "./Source"
build_path   = os.path.join(pathlib.Path.home().drive + "\\", "Temp", "GPUReshape-Build")
install_path = "../../../Binaries/ThirdParty/GPUReshape/Win64"

# All branches to pull and build
branches = [
    ("feature/raytracing",      "Raytracing",     source_path),
    ("feature/debug",           "Debug",          None),
    ("feature/device-commands", "DeviceCommands", None)
]


class JobContext:
    lock = threading.Lock()

    def __init__(self, branch_name):
        # Clean log
        log_name = os.path.join(f"Log{branch_name}.txt")
        if os.path.exists(log_name):
            os.remove(log_name)

        self.branch_name = branch_name
        self.log = open(log_name, "w")

    def line(self, contents):
        with JobContext.lock:
            sys.stdout.write(f"[{self.branch_name}] {contents}\n")
            sys.stdout.flush()
            

def clean_on_error(fn, path, e):
    if not os.access(path, os.W_OK):
        os.chmod(path, stat.S_IWUSR)
        fn(path)
    else:
        raise

def remove_directory(directory):
    if os.path.exists(directory):
        sys.stdout.write(f"Cleaning '{directory}'... ")
        shutil.rmtree(directory, ignore_errors=False, onerror=clean_on_error)
        sys.stdout.write(f"OK\n")
    
def clean_directory(directory):
    remove_directory(directory)
    os.makedirs(directory, exist_ok=True)

def call(ctx, name, args):
    ctx.line(name)
    
    # Write event in log
    ctx.log.write(f".call {str(args)}\n")
    ctx.log.flush()

    out = subprocess.run(args, stdout=ctx.log, stderr=ctx.log)
    if out.returncode != 0:
        ctx.line(f" Failed with {out.returncode}\n")
        exit(1)
    
def sync_and_build(branch):
    git_branch, branch_name, copy_source_path = branch
    
    # Paths
    build_dir   = os.path.join(build_path,   branch_name)
    install_dir = os.path.join(install_path, branch_name)

    # Create context
    ctx = JobContext(branch_name)
    
    # Clone the repository
    call(ctx, "Cloning", [
        "git", 
        "clone", "https://github.com/GPUOpen-Tools/GPU-Reshape",
        "-b", git_branch,
        build_dir
    ])
    
    # Copy clean source tree if requested
    if copy_source_path is not None:
        shutil.copytree(build_dir, copy_source_path, dirs_exist_ok=True)
        shutil.rmtree(os.path.join(copy_source_path, ".git"), ignore_errors=False, onerror=clean_on_error)

    # Generate the projects
    call(ctx, "Generating solution", [
        "cmake",
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DCMAKE_CONFIGURATION_TYPES:STRING=RelWithDebInfo",
        "-DINSTALL_THIRD_PARTY:BOOL=1",
        "-S", os.path.join(build_dir),
        "-B", os.path.join(build_dir, "cmake-build-vs2022"),
        "-DCMAKE_VS_NUGET_PACKAGE_RESTORE=ON"
    ])

    # Patch the solution
    call(ctx, "Patching solution", [
        "cmake",
        "-P", os.path.join(build_dir, "Build", "Utils", "CSProjPatch.cmake"),
        os.path.join(build_dir, "cmake-build-vs2022")
    ])

    # Restore all Nuget packages
    call(ctx, "Restoring solution", [
        "dotnet",
        "restore", os.path.join(build_dir, "cmake-build-vs2022", "GPU-Reshape.sln")
    ])

    # Actually build the project
    call(ctx, "Building", [
        os.path.join(build_dir, "cmake-build-vs2022", "GRS_MSBuild.bat"),
        os.path.join(build_dir, "cmake-build-vs2022", "GPU-Reshape.sln"),
        "/p:Configuration=RelWithDebInfo",
        "/m"
    ])

    # Package the project
    call(ctx, "Packaging", [
        os.path.join(build_dir, "Build", "Scripts", "Package.bat"),
        os.path.join("MSVC", "RelWithDebInfo")
    ])

    os.mkdir(install_dir)

    # Copy over executable
    ctx.line("Copying executables")
    shutil.copytree(
        os.path.join(build_dir, "Package", "MSVC", "RelWithDebInfo"),
        install_dir,
        dirs_exist_ok=True
    )

    # Copy over symbols
    ctx.line("Copying symbols")
    shutil.copytree(
        os.path.join(build_dir, "Package", "MSVC", "RelWithDebInfoSym"),
        install_dir,
        dirs_exist_ok=True
    )
    

if __name__ == "__main__":
    # Clean install
    clean_directory(source_path)
    clean_directory(build_path)
    clean_directory(install_path)

    # Print info
    sys.stdout.write("Syncing and building:\n")
    for branch in branches:
        sys.stdout.write(f"\t '{branch[0]}' -> {branch[1]}\n")
    sys.stdout.write("\n")
    
    # Create jobs
    jobs = []
    for branch in branches:
        job = threading.Thread(target=sync_and_build, args=(branch,))
        job.start()
        jobs.append(job)
        
    # Wait for all jobs
    for job in jobs:
        job.join()
    
    # Remove build files
    remove_directory(build_path)
