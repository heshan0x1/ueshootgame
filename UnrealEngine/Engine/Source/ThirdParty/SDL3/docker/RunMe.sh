#!/bin/bash

# Blog post for setting up arm multiarch docker images:
#   Cross Building and Running Multi-Arch Docker Images
#   https://www.ecliptik.com/Cross-Building-and-Running-Multi-Arch-Docker-Images/
#
#   this link might be more relevant for modern docker: https://www.docker.com/blog/getting-started-with-docker-for-arm-on-linux
# TL;DR:
#   apt-get install qemu-user-static
#   docker run --rm --privileged multiarch/qemu-user-static:register
#
# To test docker images, run something like this:
#   docker run -v /epic:/epic -it --platform linux/arm64 rockylinux/rockylinux:8.4 /bin/bash

SCRIPT_DIR=$(cd "$(dirname "$BASH_SOURCE")" ; pwd)
SDL_DIR=${SCRIPT_DIR}/../SDL-gui-backend

DISTRO=${1:-Rocky8}

BuildSDL3WithDocker()
{
	local Arch=$1
	local Platform=$2
	local Image=$3
	local ImageName=temp_build_linux_sdl3
	local LibDir=${SDL_DIR}/lib/Unix/${Arch}

	echo Building ${Arch}...
	echo docker run -t --name ${ImageName} --platform ${Platform} -v ${SCRIPT_DIR}/../../Vulkan:/Vulkan -v ${SDL_DIR}:/SDL-gui-backend -v ${SCRIPT_DIR}:/src ${Image} /src/docker-build-sdl3.sh
	#docker run -it --name ${ImageName} --platform ${Platform} -v ${SCRIPT_DIR}/../../Vulkan:/Vulkan -v ${SDL_DIR}:/SDL-gui-backend -v ${SCRIPT_DIR}:/src ${Image} bash -c "/src/docker-build-sdl3.sh ${DISTRO} ; bash"
	docker run -it --name ${ImageName} --platform ${Platform} -v ${SCRIPT_DIR}/../../Vulkan:/Vulkan -v ${SDL_DIR}:/SDL-gui-backend -v ${SCRIPT_DIR}:/src ${Image} bash -c "/src/docker-build-sdl3.sh ${DISTRO}"
	
	echo Copying files...
	mkdir -p ${LibDir}
	rm -rf ${LibDir}/libSDL3*.a

	docker cp ${ImageName}:/build/libSDL3_fPIC_Debug.a ${LibDir}/
	docker cp ${ImageName}:/build/libSDL3.a ${LibDir}/
	docker cp ${ImageName}:/build/libSDL3_fPIC.a ${LibDir}/

	echo Cleaning up...
	docker rm ${ImageName}
}

sudo docker run --rm --privileged docker/binfmt:820fdd95a9972a5308930a2bdfb8573dd4447ad3

if [ ${DISTRO} == "CentOS7" ]; then
	echo Building SDL on CentOS 7
	BuildSDL3WithDocker x86_64-unknown-linux-gnu      linux/amd64    centos:7
	BuildSDL3WithDocker aarch64-unknown-linux-gnueabi linux/arm64    centos:7
elif [ ${DISTRO} == "Rocky8" ]; then
	echo Building SDL on Rocky 8.4
	BuildSDL3WithDocker x86_64-unknown-linux-gnu      linux/amd64    rockylinux/rockylinux:8.4
	BuildSDL3WithDocker aarch64-unknown-linux-gnueabi linux/arm64    rockylinux/rockylinux:8.4
else
	echo Unsupported distro: ${DISTRO}
	exit 1
fi
